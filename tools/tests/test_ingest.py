import json

import pytest

from decrypt_chunk import TEST_PASSPHRASE, TEST_SALT_MAC
from nocturne_pc.db import connect
from nocturne_pc.ingest import ingest_session, ingested_session_ids, sessions_to_ingest
from segment_builder import make_segment, segment_header

STARTED = 1792205292
NOW = 1792300000

def metadata(session_id, **fields):
    result = {
        "session_id": session_id,
        "device_id": TEST_SALT_MAC,
        "started_unix": STARTED,
        "ended_unix": STARTED + 3600,
        "segment_count": 2,
        "sample_rate": 16000,
        "channels": 1,
        "sample_shift": 9,
        "codec": "opus",
        "bitrate": 32000,
        "opus_application": "audio",
        "encryption": "aes-256-gcm",
        "chunk_frames": 50,
        "firmware_version": "v0.3.2",
        "time_reliable": True,
        "resumed": False,
        "resume_count": 0,
        "resumes": [],
        "recovered": False,
        "muted_ms": 960,
        "muted": [{"segment": 1, "start_ms": 142380, "ms": 960}],
        "muted_truncated": False,
        "health": {"buffer_overflows": 0, "sd_write_errors": 1, "upload_retries": 3, "wifi_disconnects": 1},
    }
    result.update(fields)
    return result

def write_session(audio_dir, key, fields, frames_per_segment):
    folder = audio_dir / fields["session_id"]
    folder.mkdir(parents=True)

    for segment, frames_per_chunk in frames_per_segment.items():
        data = make_segment(key, segment, frames_per_chunk)
        (folder / f"seg_{segment:04d}.opus.enc").write_bytes(data)

    (folder / "session.json").write_text(json.dumps(fields))
    return folder

@pytest.fixture
def connection(tmp_path):
    database = connect(tmp_path / "nocturne.duckdb")
    yield database
    database.close()

def rows(connection, query, session_id):
    return connection.execute(query, [session_id]).fetchall()

def test_a_session_lands_in_every_table(tmp_path, kat_key, connection):
    folder = write_session(tmp_path, kat_key, metadata("20261017_0248"), {0: [50, 50], 1: [50, 10]})

    ingest_session(connection, folder, TEST_PASSPHRASE, NOW)

    session = connection.execute(
        "SELECT device_id, started_unix, segment_count, sample_shift, muted_ms, sd_write_errors, "
        "upload_retries, ingested_unix FROM sessions").fetchall()
    assert session == [(TEST_SALT_MAC, STARTED, 2, 9, 960, 1, 3, NOW)]
    assert rows(connection, "SELECT mute, segment, start_ms, ms FROM mutes WHERE session_id = ?",
                "20261017_0248") == [(1, 1, 142380, 960)]
    assert rows(connection, "SELECT segment, frames, chunks, torn, corrupt FROM segments "
                "WHERE session_id = ? ORDER BY segment", "20261017_0248") == [
        (0, 100, 2, False, False),
        (1, 60, 2, False, False),
    ]
    assert rows(connection, "SELECT * FROM outages WHERE session_id = ?", "20261017_0248") == []

def test_a_resume_places_its_outage_after_the_audio_before_it(tmp_path, kat_key, connection):
    anchor = STARTED + 2000
    fields = metadata("20261017_0248", segment_count=3, resumed=True, resume_count=1,
                      resumes=[{"segment": 2, "started_unix": anchor}])
    folder = write_session(tmp_path, kat_key, fields, {0: [50, 50], 1: [50, 10], 2: [50]})
    torn = folder / "seg_0001.opus.enc"
    torn.write_bytes(torn.read_bytes() + bytes(20))

    ingest_session(connection, folder, TEST_PASSPHRASE, NOW)

    assert rows(connection, "SELECT resume, segment, anchor_unix FROM resumes WHERE session_id = ?",
                "20261017_0248") == [(1, 2, anchor)]
    assert rows(connection, "SELECT torn FROM segments WHERE session_id = ? AND segment = 1",
                "20261017_0248") == [(True,)]
    outages = rows(connection, "SELECT run, segment, start_unix, end_unix FROM outages WHERE session_id = ?",
                   "20261017_0248")
    assert outages == [(1, 2, pytest.approx(STARTED + 160 * 0.02), anchor)]

def test_an_outage_with_a_segment_missing_before_it_has_no_start(tmp_path, kat_key, connection):
    fields = metadata("20261017_0248", segment_count=3, resumes=[{"segment": 2, "started_unix": STARTED + 2000}])
    write_session(tmp_path, kat_key, fields, {0: [50], 2: [50]})

    ingest_session(connection, tmp_path / "20261017_0248", TEST_PASSPHRASE, NOW)

    assert rows(connection, "SELECT start_unix FROM outages WHERE session_id = ?", "20261017_0248") == [(None,)]

def test_a_resume_that_reused_its_segment_starts_at_the_one_before(tmp_path, kat_key, connection):
    first = STARTED + 2000
    second = STARTED + 2500
    fields = metadata("20261017_0248", resumes=[{"segment": 1, "started_unix": first},
                                                {"segment": 1, "started_unix": second}])
    write_session(tmp_path, kat_key, fields, {0: [50], 1: [50]})

    ingest_session(connection, tmp_path / "20261017_0248", TEST_PASSPHRASE, NOW)

    outages = rows(connection, "SELECT run, start_unix, end_unix FROM outages WHERE session_id = ? ORDER BY run",
                   "20261017_0248")
    assert outages == [(1, pytest.approx(STARTED + 1.0), first), (2, pytest.approx(first), second)]

def test_ingesting_again_replaces_the_session(tmp_path, kat_key, connection):
    folder = write_session(tmp_path, kat_key, metadata("20261017_0248"), {0: [50]})
    ingest_session(connection, folder, TEST_PASSPHRASE, NOW)

    (folder / "session.json").write_text(json.dumps(metadata("20261017_0248", muted=[])))
    ingest_session(connection, folder, TEST_PASSPHRASE, NOW + 60)

    assert connection.execute("SELECT COUNT(*), MAX(ingested_unix) FROM sessions").fetchone() == (1, NOW + 60)
    assert connection.execute("SELECT COUNT(*) FROM mutes").fetchone() == (0,)
    assert connection.execute("SELECT COUNT(*) FROM segments").fetchone() == (1,)

def test_an_older_session_json_leaves_the_new_fields_empty(tmp_path, kat_key, connection):
    fields = metadata("20261017_0248")
    for name in ("sample_shift", "resumes", "muted", "muted_ms", "muted_truncated"):
        del fields[name]
    folder = write_session(tmp_path, kat_key, fields, {0: [50]})

    ingest_session(connection, folder, TEST_PASSPHRASE, NOW)

    assert connection.execute("SELECT sample_shift, muted_ms FROM sessions").fetchall() == [(None, None)]
    assert connection.execute("SELECT COUNT(*) FROM mutes").fetchone() == (0,)

def test_the_wrong_passphrase_is_refused_and_nothing_is_stored(tmp_path, kat_key, connection):
    folder = write_session(tmp_path, kat_key, metadata("20261017_0248"), {0: [50, 50]})

    with pytest.raises(ValueError, match="wrong passphrase"):
        ingest_session(connection, folder, "not the passphrase", NOW)

    assert ingested_session_ids(connection) == set()

def test_a_header_only_segment_is_not_taken_for_a_wrong_key(tmp_path, kat_key, connection):
    folder = write_session(tmp_path, kat_key, metadata("20261017_0248", segment_count=1), {})
    (folder / "seg_0000.opus.enc").write_bytes(segment_header())

    ingest_session(connection, folder, TEST_PASSPHRASE, NOW)

    assert connection.execute("SELECT frames, chunks FROM segments").fetchall() == [(0, 0)]

def test_a_folder_naming_another_session_is_refused(tmp_path, kat_key, connection):
    folder = write_session(tmp_path, kat_key, metadata("20261017_0248"), {0: [50]})
    renamed = folder.rename(tmp_path / "20261018_0010")

    with pytest.raises(ValueError, match="names session"):
        ingest_session(connection, renamed, TEST_PASSPHRASE, NOW)

def test_only_new_finished_sessions_are_picked_unless_forced(tmp_path):
    audio = tmp_path / "audio"
    for name in ("done", "new", "no_marker", ".partial"):
        (audio / name).mkdir(parents=True)
    (audio / "done" / "session.json").write_text("{}")
    (audio / "new" / "session.json").write_text("{}")

    picked = sessions_to_ingest(audio, {"done"}, force=False)
    forced = sessions_to_ingest(audio, {"done"}, force=True)

    assert [folder.name for folder in picked] == ["new"]
    assert [folder.name for folder in forced] == ["done", "new"]
    assert sessions_to_ingest(tmp_path / "missing", set(), force=False) == []
