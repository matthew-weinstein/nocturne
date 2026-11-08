"""Load each synced session's metadata into DuckDB (design doc 9.5).

    cd tools
    python -m nocturne_pc.ingest
    python -m nocturne_pc.ingest --force

Every audio/<session_id>/ holding session.json that is not yet in the
database is read into sessions, resumes and mutes. Its segments are decrypted
(not decoded) to count frames into segments, which places each resume's
outage in the outages view. --force re-reads every session still in audio/.

A session is replaced whole in one transaction, so a rerun never duplicates
rows. The passphrase comes from NOCTURNE_PASSPHRASE or a prompt.
"""

import argparse
import json
import sys
import time

from decrypt_segment import SEGMENT_HEADER_BYTES, SEGMENT_NAME, walk_segment
from nocturne_pc import data_root
from nocturne_pc.audio import device_key
from nocturne_pc.db import connect
from nocturne_pc.passphrase import read_passphrase

COMPLETION_MARKER = "session.json"
TABLES = ("sessions", "resumes", "mutes", "segments")
FRAME_SECONDS = 0.02

def session_row(metadata, ingested_unix):
    health = metadata.get("health", {})
    return (
        metadata["session_id"],
        metadata["device_id"],
        metadata["started_unix"],
        metadata["ended_unix"],
        metadata["segment_count"],
        metadata.get("sample_rate"),
        metadata.get("channels"),
        metadata.get("sample_shift"),
        metadata.get("codec"),
        metadata.get("bitrate"),
        metadata.get("opus_application"),
        metadata.get("encryption"),
        metadata.get("chunk_frames"),
        metadata.get("firmware_version"),
        metadata.get("time_reliable"),
        metadata.get("resumed"),
        metadata.get("resume_count"),
        metadata.get("recovered"),
        metadata.get("muted_ms"),
        metadata.get("muted_truncated"),
        health.get("buffer_overflows"),
        health.get("sd_write_errors"),
        health.get("upload_retries"),
        health.get("wifi_disconnects"),
        ingested_unix,
    )

def resume_rows(metadata):
    rows = []
    resume = 1

    for anchor in metadata.get("resumes", []):
        rows.append((metadata["session_id"], resume, anchor["segment"], anchor["started_unix"]))
        resume += 1

    return rows

def mute_rows(metadata):
    rows = []
    mute = 1

    for muted in metadata.get("muted", []):
        rows.append((metadata["session_id"], mute, muted["segment"], muted["start_ms"], muted["ms"]))
        mute += 1

    return rows

def segment_paths(session_dir):
    paths = {}
    for path in sorted(session_dir.iterdir()):
        match = SEGMENT_NAME.search(path.name)
        if match:
            paths[int(match.group(1))] = path
    return paths

def segment_rows(session_id, session_dir, key):
    """One row per segment file, and any segment that failed mid-way."""
    rows = []
    corrupt = []
    num_with_records = 0
    num_opened = 0

    for segment, path in segment_paths(session_dir).items():
        result = walk_segment(key, path, segment)

        if result.file_bytes > SEGMENT_HEADER_BYTES:
            num_with_records += 1
        if result.num_chunks > 0:
            num_opened += 1
        if result.errors:
            corrupt.append(f"{path.name}: {'; '.join(result.errors)}")

        torn = result.stop_reason is not None
        is_corrupt = len(result.errors) > 0
        rows.append((session_id, segment, len(result.packets), result.num_chunks, result.file_bytes, torn, is_corrupt))

    if num_with_records > 0 and num_opened == 0:
        raise ValueError(f"no segment of {session_id} opens: wrong passphrase, or another device's session")

    return rows, corrupt

def store_session(connection, session, resumes, mutes, segments):
    session_id = session[0]
    connection.begin()
    try:
        for table in TABLES:
            connection.execute(f"DELETE FROM {table} WHERE session_id = ?", [session_id])

        placeholders = ", ".join(["?"] * len(session))
        connection.execute(f"INSERT INTO sessions VALUES ({placeholders})", list(session))
        if resumes:
            connection.executemany("INSERT INTO resumes VALUES (?, ?, ?, ?)", resumes)
        if mutes:
            connection.executemany("INSERT INTO mutes VALUES (?, ?, ?, ?, ?)", mutes)
        if segments:
            connection.executemany("INSERT INTO segments VALUES (?, ?, ?, ?, ?, ?, ?)", segments)
    except Exception:
        connection.rollback()
        raise

    connection.commit()

def ingest_session(connection, session_dir, passphrase, ingested_unix):
    metadata = json.loads((session_dir / COMPLETION_MARKER).read_text(encoding="utf-8"))
    session_id = metadata["session_id"]
    if session_id != session_dir.name:
        raise ValueError(f"{session_dir.name}/session.json names session {session_id}")

    key = device_key(metadata["device_id"], passphrase)
    segments, corrupt = segment_rows(session_id, session_dir, key)

    store_session(connection, session_row(metadata, ingested_unix), resume_rows(metadata),
                  mute_rows(metadata), segments)
    return metadata, segments, corrupt

def ingested_session_ids(connection):
    ids = set()
    for row in connection.execute("SELECT session_id FROM sessions").fetchall():
        ids.add(row[0])
    return ids

def sessions_to_ingest(audio_dir, ingested, force):
    folders = []
    if not audio_dir.exists():
        return folders

    for folder in sorted(audio_dir.iterdir()):
        if not (folder / COMPLETION_MARKER).exists():
            continue
        if folder.name in ingested and not force:
            continue
        folders.append(folder)

    return folders

def report(connection, metadata, segments, corrupt):
    session_id = metadata["session_id"]
    num_frames = 0
    for row in segments:
        num_frames += row[2]

    print(f"ingested {session_id}: {len(segments)} of {metadata['segment_count']} segments, "
          f"{num_frames} frames ({num_frames * FRAME_SECONDS:.0f} s), "
          f"{len(metadata.get('resumes', []))} resumes, {len(metadata.get('muted', []))} mutes")

    for problem in corrupt:
        print(f"  WARNING: {problem}")

    outages = connection.execute(
        "SELECT segment, start_unix, end_unix FROM outages WHERE session_id = ? ORDER BY run",
        [session_id]).fetchall()
    for segment, start_unix, end_unix in outages:
        if start_unix is None:
            print(f"  outage before segment {segment}: ended {end_unix}, start unknown (segments missing)")
        else:
            print(f"  outage before segment {segment}: {end_unix - start_unix:.0f} s")

def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--force", action="store_true", help="re-read sessions already in the database")
    args = parser.parse_args()

    audio_dir = data_root() / "audio"
    connection = connect()

    folders = sessions_to_ingest(audio_dir, ingested_session_ids(connection), args.force)
    if folders:
        passphrase = read_passphrase()
    now = int(time.time())

    failed = False
    num_ingested = 0
    for folder in folders:
        try:
            metadata, segments, corrupt = ingest_session(connection, folder, passphrase, now)
        except KeyError as error:
            print(f"FAIL: {folder.name}: session.json has no {error}")
            failed = True
            continue
        except ValueError as error:
            print(f"FAIL: {folder.name}: {error}")
            failed = True
            continue
        report(connection, metadata, segments, corrupt)
        num_ingested += 1

    total = connection.execute("SELECT COUNT(*) FROM sessions").fetchone()[0]
    print(f"database: {total} sessions, {num_ingested} read this run")

    if failed:
        sys.exit(1)

if __name__ == "__main__":
    main()
