"""The dashboard's local server: JSON per page, audio clips and the built front end.

    cd tools
    python -m nocturne_pc.dashboard
    python -m nocturne_pc.dashboard --port 8484 --no-audio

Listens on 127.0.0.1 only and answers only requests addressed to it, so a web
page cannot reach it through a rebound name. The database is opened read-only
per request, so the pipeline can write between them. Audio is decoded on
demand and never written to disk; the passphrase is read once at startup.

    GET /api/status
    GET /api/nights
    GET /api/night/<session_id>
    GET /api/trends?from=2026-10-01&to=2026-11-01
    GET /api/audio/<session_id>?segment=3&frame=120&frames=250    (WAV)
"""

import argparse
import json
import mimetypes
import re
import threading
from collections import OrderedDict
from datetime import date
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlsplit

import duckdb

from nocturne_pc import data_root
from nocturne_pc.audio import SAMPLE_RATE_HZ, SessionAudio
from nocturne_pc.dashboard_api import clip_frames, list_nights, night_detail, status, trend_nights, wav_bytes
from nocturne_pc.passphrase import read_passphrase
from nocturne_pc.teacher import SOURCE

HOST = "127.0.0.1"
DEFAULT_PORT = 8484
STATIC_DIR = Path(__file__).resolve().parent.parent / "dashboard" / "dist"
SESSION_ID = re.compile(r"^[0-9]{8}_[0-9]{4}$")
CACHED_SESSIONS = 2

class AudioCache:
    """Open sessions' decoders, the most recently used kept; one decode at a time."""

    def __init__(self, passphrase):
        self.passphrase = passphrase
        self.sessions = OrderedDict()
        self.lock = threading.Lock()

    def clip(self, session_id, segment, first_frame, num_frames):
        with self.lock:
            audio = self.sessions.get(session_id)
            if audio is None:
                audio = SessionAudio.open(session_id, self.passphrase)
                self.sessions[session_id] = audio
                while len(self.sessions) > CACHED_SESSIONS:
                    self.sessions.popitem(last=False)
            self.sessions.move_to_end(session_id)
            return audio.decode(segment, first_frame, num_frames)

def parse_night(text):
    try:
        return date.fromisoformat(text)
    except (TypeError, ValueError):
        return None

def query_int(query, name):
    values = query.get(name)
    if not values:
        return None
    try:
        return int(values[0])
    except ValueError:
        return None

def static_path(root, url_path):
    """The file under root a URL path names, index.html for the root, or None if it leaves root."""
    relative = url_path.lstrip("/")
    if relative == "":
        relative = "index.html"
    candidate = (root / relative).resolve()
    if not candidate.is_relative_to(root.resolve()):
        return None
    return candidate

def query_text(query, name):
    values = query.get(name)
    if not values:
        return None
    return values[0]

def make_handler(database_path, audio_cache, static_dir):
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, format, *args):
            pass

        def send_body(self, code, content_type, body):
            self.send_response(code)
            self.send_header("Content-Type", content_type)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.send_header("X-Content-Type-Options", "nosniff")
            self.end_headers()
            self.wfile.write(body)

        def send_json(self, value, code=HTTPStatus.OK):
            body = json.dumps(value, separators=(",", ":")).encode("utf-8")
            self.send_body(code, "application/json", body)

        def send_error_json(self, code, message):
            self.send_json({"error": message}, code)

        def addressed_here(self):
            port = self.server.server_address[1]
            allowed = {f"{HOST}:{port}", f"localhost:{port}"}
            return self.headers.get("Host") in allowed

        def do_GET(self):
            if not self.addressed_here():
                self.send_error_json(HTTPStatus.FORBIDDEN, "wrong host")
                return

            url = urlsplit(self.path)
            query = parse_qs(url.query)
            parts = url.path.strip("/").split("/")
            if parts[0] != "api":
                self.serve_static(url.path)
                return

            try:
                with duckdb.connect(str(database_path), read_only=True) as connection:
                    self.route(connection, parts[1:], query)
            except duckdb.IOException:
                self.send_error_json(HTTPStatus.SERVICE_UNAVAILABLE, "database busy")
            except duckdb.Error as error:
                self.send_error_json(HTTPStatus.INTERNAL_SERVER_ERROR, str(error))

        def route(self, connection, parts, query):
            if parts == ["status"]:
                self.send_json(status(connection))
                return
            if parts == ["nights"]:
                self.send_json(list_nights(connection, SOURCE))
                return
            if parts == ["trends"]:
                first = parse_night(query_text(query, "from"))
                last = parse_night(query_text(query, "to"))
                if first is None or last is None:
                    self.send_error_json(HTTPStatus.BAD_REQUEST, "from and to must be dates")
                    return
                self.send_json(trend_nights(connection, SOURCE, first, last))
                return
            if len(parts) == 2 and SESSION_ID.match(parts[1]):
                if parts[0] == "night":
                    detail = night_detail(connection, parts[1], SOURCE)
                    if detail is None:
                        self.send_error_json(HTTPStatus.NOT_FOUND, "no such night")
                        return
                    self.send_json(detail)
                    return
                if parts[0] == "audio":
                    self.serve_audio(parts[1], query)
                    return
            self.send_error_json(HTTPStatus.NOT_FOUND, "no such route")

        def serve_audio(self, session_id, query):
            if audio_cache is None:
                self.send_error_json(HTTPStatus.SERVICE_UNAVAILABLE, "audio is off")
                return
            segment = query_int(query, "segment")
            first_frame = query_int(query, "frame")
            num_frames = query_int(query, "frames")
            if segment is None or first_frame is None or num_frames is None:
                self.send_error_json(HTTPStatus.BAD_REQUEST, "segment, frame and frames are required")
                return
            try:
                samples = audio_cache.clip(session_id, segment, first_frame, clip_frames(num_frames))
            except (FileNotFoundError, ValueError) as error:
                self.send_error_json(HTTPStatus.NOT_FOUND, str(error))
                return
            self.send_body(HTTPStatus.OK, "audio/wav", wav_bytes(samples, SAMPLE_RATE_HZ))

        def serve_static(self, url_path):
            path = static_path(static_dir, url_path)
            if path is None or not path.is_file():
                self.send_error_json(HTTPStatus.NOT_FOUND, "not found")
                return
            content_type = mimetypes.guess_type(path.name)[0]
            if content_type is None:
                content_type = "application/octet-stream"
            self.send_body(HTTPStatus.OK, content_type, path.read_bytes())

    return Handler

def default_database():
    return data_root() / "nocturne.duckdb"

def serve_in_background(database_path, audio_cache, port=0):
    """A running server on 127.0.0.1, on a free port by default; stop it with shutdown()."""
    server = ThreadingHTTPServer((HOST, port), make_handler(database_path, audio_cache, STATIC_DIR))
    thread = threading.Thread(target=server.serve_forever, name="dashboard", daemon=True)
    thread.start()
    return server

def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--port", type=int, default=DEFAULT_PORT)
    parser.add_argument("--database", type=Path, default=None, help="default: nocturne.duckdb at the data root")
    parser.add_argument("--no-audio", action="store_true", help="serve pages without the passphrase")
    args = parser.parse_args()

    database_path = args.database
    if database_path is None:
        database_path = default_database()

    audio_cache = None
    if not args.no_audio:
        audio_cache = AudioCache(read_passphrase())

    handler = make_handler(database_path, audio_cache, STATIC_DIR)
    server = ThreadingHTTPServer((HOST, args.port), handler)
    print(f"dashboard on http://{HOST}:{args.port}/")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()

if __name__ == "__main__":
    main()
