import io
import json
import threading
import urllib.error
import urllib.request
import wave
from datetime import date
from http.server import ThreadingHTTPServer

import numpy as np
import pytest

from nocturne_pc.dashboard import HOST, make_handler, static_path
from nocturne_pc.dashboard_api import (
    MAX_CLIP_FRAMES,
    clip_frames,
    level_envelope,
    list_nights,
    neighbours,
    night_detail,
    trend_nights,
    wav_bytes,
)
from nocturne_pc.db import connect
from nocturne_pc.nights import NIGHT_COLUMNS, full_scale_db_spl
from nocturne_pc.teacher import SOURCE
from nocturne_pc.trends import load_settings, store_settings

BEDTIME = 1794128400
HOUR = 3600
SEGMENT_FRAMES = 15000
SEGMENT_S = SEGMENT_FRAMES * 0.02

def add_night(connection, session_id, night_of, bedtime, hours):
    up = bedtime + hours * HOUR
    connection.execute("INSERT INTO sessions (session_id, device_id, started_unix, ended_unix, segment_count, "
                       "sample_shift, ingested_unix) VALUES (?, 'aa', ?, ?, 2, 9, 5)", [session_id, bedtime, up])
    for segment in range(2):
        connection.execute("INSERT INTO segments VALUES (?, ?, ?, 300, 1000, false, false)",
                           [session_id, segment, SEGMENT_FRAMES])

    row = {}
    for name in NIGHT_COLUMNS:
        row[name] = 0
    row.update({
        "session_id": session_id, "source": SOURCE, "rules_sha256": "",
        "bedtime_unix": bedtime, "up_unix": up, "time_in_bed_min": hours * 60,
        "onset_unix": bedtime + 600, "onset_latency_min": 10, "wake_unix": up - 300, "lie_in_min": 5,
        "sleep_min": hours * 60 - 15, "efficiency": 0.95, "midpoint_unix": bedtime + hours * HOUR / 2,
        "snoring_peak_db": None, "snoring_worst_hour_unix": None, "breathing_pauses_per_hour": None,
        "first_alarm_unix": None, "last_alarm_unix": None, "snooze_span_min": None, "alarm_to_up_min": None,
        "noise_floor_db": 41.0, "snoring_share": None,
    })
    values = []
    for name in NIGHT_COLUMNS:
        values.append(row[name])
    connection.execute(f"INSERT INTO nights ({', '.join(NIGHT_COLUMNS)}) VALUES ({', '.join(['?'] * len(values))})",
                       values)
    connection.execute("INSERT INTO night_context VALUES (?, ?, ?, false, 'America/Toronto', -300, NULL, NULL, ?, ?)",
                       [session_id, night_of, night_of.isoweekday(), bedtime - 8 * HOUR, up + HOUR])

def add_event(connection, session_id, class_name, segment, frame, start_unix, seconds):
    connection.execute("INSERT INTO events VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, 1, 0.9, 0.8)",
                       [session_id, SOURCE, class_name, segment, frame, segment, frame + seconds * 50,
                        start_unix, start_unix + seconds, seconds * 1000])

def add_level(connection, session_id, segment, frame, peak_dbfs):
    connection.execute("INSERT INTO levels VALUES (?, ?, ?, -60, ?)", [session_id, segment, frame, peak_dbfs])

@pytest.fixture
def database_path(tmp_path):
    path = tmp_path / "nocturne.duckdb"
    connection = connect(path)
    store_settings(connection, load_settings())
    add_night(connection, "20261108_0410", date(2026, 11, 7), BEDTIME, 8)
    add_night(connection, "20261109_0116", date(2026, 11, 8), BEDTIME + 21 * HOUR, 5)
    add_night(connection, "20261110_0002", date(2026, 11, 9), BEDTIME + 44 * HOUR, 7)
    add_event(connection, "20261108_0410", "snoring", 0, 1000, BEDTIME + 20, 30)
    add_event(connection, "20261108_0410", "cough", 1, 50, BEDTIME + SEGMENT_S + 1, 1)
    add_level(connection, "20261108_0410", 0, 0, -40)
    add_level(connection, "20261108_0410", 0, 48, -30)
    add_level(connection, "20261108_0410", 1, 0, -50)
    connection.close()
    return path

@pytest.fixture
def connection(database_path):
    database = connect(database_path)
    yield database
    database.close()

def test_nights_list_newest_first_with_their_times(connection):
    nights = list_nights(connection, SOURCE)

    session_ids = []
    for night in nights:
        session_ids.append(night["session_id"])
    assert session_ids == ["20261110_0002", "20261109_0116", "20261108_0410"]
    assert nights[2]["night_of"] == "2026-11-07"
    assert nights[2]["bedtime_unix"] == BEDTIME
    assert nights[2]["sunrise_unix"] == BEDTIME + 9 * HOUR

def test_neighbours_are_the_older_and_newer_nights(connection):
    nights = list_nights(connection, SOURCE)

    assert neighbours(nights, "20261109_0116") == ("20261108_0410", "20261110_0002")
    assert neighbours(nights, "20261108_0410") == (None, "20261109_0116")
    assert neighbours(nights, "20261110_0002") == ("20261109_0116", None)
    assert neighbours(nights, "20990101_0000") == (None, None)

def test_a_night_has_its_metrics_events_and_levels(connection):
    detail = night_detail(connection, "20261108_0410", SOURCE, bin_s=60)

    assert detail["session"]["segment_count"] == 2
    assert detail["metrics"]["sleep_min"] == 8 * 60 - 15
    assert "rules_sha256" not in detail["metrics"]
    assert detail["context"]["night_of"] == "2026-11-07"
    assert detail["previous"] is None
    assert detail["next"] == "20261109_0116"

    classes = []
    for event in detail["events"]:
        classes.append(event["class"])
    assert classes == ["snoring", "cough"]
    assert detail["events"][1]["start_segment"] == 1

    offset = full_scale_db_spl(9)
    peaks = detail["levels"]["peak_db"]
    assert detail["levels"]["start_unix"] == BEDTIME
    assert len(peaks) == 8 * 60
    assert peaks[0] == round(-30 + offset, 1)
    assert peaks[5] == round(-50 + offset, 1)
    assert peaks[1] is None

def test_a_session_without_a_night_has_no_detail(connection):
    assert night_detail(connection, "20990101_0000", SOURCE) is None
    assert night_detail(connection, "20261108_0410", "device:other") is None

def test_the_envelope_keeps_each_bins_loudest_peak_inside_the_span():
    levels = [(95, -10), (100, -40), (104, -20), (109.9, -30), (110, -35), (130, -5)]

    assert level_envelope(levels, 100, 130, 10, 80) == [60, 45, None]

def test_trends_cover_the_asked_nights_oldest_first(connection):
    rows = trend_nights(connection, SOURCE, date(2026, 11, 8), date(2026, 11, 30))

    nights = []
    for row in rows:
        nights.append(row["night_of"])
    assert nights == ["2026-11-08", "2026-11-09"]
    assert rows[0]["sleep_min"] == 5 * 60 - 15

def test_clips_are_limited_to_ten_minutes():
    assert clip_frames(0) == 1
    assert clip_frames(250) == 250
    assert clip_frames(10 ** 9) == MAX_CLIP_FRAMES

def test_samples_become_a_16_bit_wav():
    body = wav_bytes(np.array([0.0, 0.5, -1.0, 2.0], dtype=np.float32), 16000)

    with wave.open(io.BytesIO(body)) as file:
        assert file.getnchannels() == 1
        assert file.getsampwidth() == 2
        assert file.getframerate() == 16000
        samples = np.frombuffer(file.readframes(4), dtype="<i2")
    assert list(samples) == [0, 16384, -32767, 32767]

def test_static_paths_stay_inside_the_root(tmp_path):
    root = tmp_path / "dist"
    root.mkdir()

    assert static_path(root, "/") == (root / "index.html").resolve()
    assert static_path(root, "/assets/app.js") == (root / "assets" / "app.js").resolve()
    assert static_path(root, "/../secret.txt") is None
    assert static_path(root, "/assets/../../secret.txt") is None

@pytest.fixture
def server(database_path, tmp_path):
    static_dir = tmp_path / "dist"
    static_dir.mkdir()
    (static_dir / "index.html").write_text("<!doctype html><title>Nocturne</title>", encoding="utf-8")
    httpd = ThreadingHTTPServer((HOST, 0), make_handler(database_path, None, static_dir))
    thread = threading.Thread(target=httpd.serve_forever, daemon=True)
    thread.start()
    yield httpd.server_address[1]
    httpd.shutdown()
    httpd.server_close()

def get(port, path, host=None):
    request = urllib.request.Request(f"http://{HOST}:{port}{path}")
    if host is not None:
        request.add_header("Host", host)
    try:
        with urllib.request.urlopen(request) as response:
            return response.status, response.headers.get("Content-Type"), response.read()
    except urllib.error.HTTPError as error:
        return error.code, error.headers.get("Content-Type"), error.read()

def test_the_server_answers_each_page_as_json(server):
    code, content_type, body = get(server, "/api/nights")
    assert code == 200
    assert content_type == "application/json"
    assert len(json.loads(body)) == 3

    code, _, body = get(server, "/api/night/20261109_0116")
    assert code == 200
    assert json.loads(body)["previous"] == "20261108_0410"

    code, _, body = get(server, "/api/trends?from=2026-11-01&to=2026-11-30")
    assert code == 200
    assert len(json.loads(body)) == 3

    code, _, body = get(server, "/api/status")
    assert json.loads(body) == {"last_ingested_unix": 5, "sessions": 3}

def test_the_server_refuses_bad_requests(server):
    assert get(server, "/api/night/20990101_0000")[0] == 404
    assert get(server, "/api/night/..%2Fsecret")[0] == 404
    assert get(server, "/api/trends?from=soon&to=later")[0] == 400
    assert get(server, "/api/audio/20261108_0410?segment=0&frame=0&frames=50")[0] == 503
    assert get(server, "/api/nowhere")[0] == 404

def test_the_server_answers_only_its_own_host(server):
    assert get(server, "/api/nights", host="evil.example")[0] == 403
    assert get(server, "/api/nights", host=f"localhost:{server}")[0] == 200

def test_the_server_serves_the_front_end(server):
    code, content_type, body = get(server, "/")
    assert code == 200
    assert content_type == "text/html"
    assert b"Nocturne" in body
    assert get(server, "/missing.js")[0] == 404
