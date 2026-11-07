from datetime import date

from nocturne_pc.sync import (
    BucketSession,
    append_ledger,
    bytes_synced_on,
    parse_bucket_listing,
    parse_mod_time,
    plan_downloads,
    read_ledger,
    sessions_at_risk,
    synced_session_ids,
)

DAY = 86400
NOW = 1792300000
MB = 1_000_000

def entry(path, size, mod_time):
    return {"Path": path, "Name": path.split("/")[-1], "Size": size, "ModTime": mod_time, "IsDir": False}

def session(session_id, num_bytes, first_upload_unix, complete=True):
    result = BucketSession(session_id)
    result.num_bytes = num_bytes
    result.first_upload_unix = first_upload_unix
    result.complete = complete
    return result

def test_mod_times_with_nanoseconds_and_offsets_parse():
    assert parse_mod_time("2026-10-17T02:48:12Z") == 1792205292
    assert parse_mod_time("2026-10-17T02:48:12.123456789Z") == 1792205292
    assert parse_mod_time("2026-10-16T22:48:12.5-04:00") == 1792205292
    assert parse_mod_time("2026-10-17T04:48:12+02:00") == 1792205292

def test_files_are_grouped_into_sessions_oldest_first():
    sessions = parse_bucket_listing([
        entry("20261017_0248/seg_0000.opus.enc", 1200000, "2026-10-17T02:53:20Z"),
        entry("20261016_2300/seg_0000.opus.enc", 1100000, "2026-10-16T23:05:00Z"),
        entry("20261017_0248/seg_0001.opus.enc", 1150000, "2026-10-17T02:58:20Z"),
        entry("20261017_0248/session.json", 900, "2026-10-17T03:00:00Z"),
    ])

    assert [s.session_id for s in sessions] == ["20261016_2300", "20261017_0248"]
    assert sessions[0].complete is False
    assert sessions[1].complete is True
    assert sessions[1].num_bytes == 1200000 + 1150000 + 900
    assert sessions[1].first_upload_unix == parse_mod_time("2026-10-17T02:53:20Z")

def test_a_file_outside_a_session_folder_is_ignored():
    sessions = parse_bucket_listing([entry("stray.txt", 10, "2026-10-17T02:53:20Z")])

    assert sessions == []

def test_a_session_json_deeper_than_the_session_folder_does_not_complete_it():
    sessions = parse_bucket_listing([entry("20261017_0248/old/session.json", 900, "2026-10-17T03:00:00Z")])

    assert sessions[0].complete is False

def test_the_plan_takes_finished_unsynced_sessions_oldest_first():
    sessions = [
        session("a", 100 * MB, NOW - 3 * DAY),
        session("b", 100 * MB, NOW - 2 * DAY, complete=False),
        session("c", 100 * MB, NOW - 2 * DAY),
        session("d", 100 * MB, NOW - DAY),
    ]

    planned = plan_downloads(sessions, {"c"}, 900 * MB, 0)

    assert [s.session_id for s in planned] == ["a", "d"]

def test_the_plan_stops_at_the_first_session_past_the_cap():
    sessions = [
        session("a", 400 * MB, NOW - 3 * DAY),
        session("b", 400 * MB, NOW - 2 * DAY),
        session("c", 50 * MB, NOW - DAY),
    ]

    planned = plan_downloads(sessions, set(), 900 * MB, 200 * MB)

    assert [s.session_id for s in planned] == ["a"]

def test_a_session_exactly_at_the_cap_is_taken():
    sessions = [session("a", 900 * MB, NOW)]

    assert len(plan_downloads(sessions, set(), 900 * MB, 0)) == 1
    assert len(plan_downloads(sessions, set(), 900 * MB, 1)) == 0

def test_a_finished_session_is_at_risk_from_21_days():
    sessions = [
        session("young", MB, NOW - 21 * DAY + 1),
        session("old", MB, NOW - 21 * DAY),
        session("synced", MB, NOW - 25 * DAY),
        session("unfinished", MB, NOW - 25 * DAY, complete=False),
    ]

    at_risk = sessions_at_risk(sessions, {"synced"}, NOW)

    assert [s.session_id for s in at_risk] == ["old"]

def test_the_ledger_round_trips_and_a_missing_one_is_empty(tmp_path):
    path = tmp_path / "synced.jsonl"
    assert read_ledger(path) == []

    append_ledger(path, "20261017_0248", NOW, 28 * MB)
    append_ledger(path, "20261018_0010", NOW + 60, 29 * MB)

    ledger = read_ledger(path)
    assert ledger == [
        {"session_id": "20261017_0248", "synced_unix": NOW, "bytes": 28 * MB},
        {"session_id": "20261018_0010", "synced_unix": NOW + 60, "bytes": 29 * MB},
    ]

def test_the_days_total_counts_only_that_utc_day():
    midnight = 1792281600
    ledger = [
        {"session_id": "a", "synced_unix": midnight - 1, "bytes": 300 * MB},
        {"session_id": "b", "synced_unix": midnight, "bytes": 100 * MB},
        {"session_id": "c", "synced_unix": midnight + DAY - 1, "bytes": 50 * MB},
    ]

    assert bytes_synced_on(ledger, date(2026, 10, 18)) == 150 * MB
    assert bytes_synced_on(ledger, date(2026, 10, 17)) == 300 * MB

def test_a_pruned_session_stays_synced_and_a_partial_one_does_not(tmp_path):
    audio = tmp_path / "audio"
    (audio / "held").mkdir(parents=True)
    (audio / "held" / "session.json").write_text("{}")
    (audio / ".partial" / "half").mkdir(parents=True)
    (audio / "no_marker").mkdir()
    ledger = [{"session_id": "pruned", "synced_unix": NOW, "bytes": MB}]

    assert synced_session_ids(ledger, audio) == {"held", "pruned"}

def test_no_audio_folder_leaves_only_the_ledger(tmp_path):
    ledger = [{"session_id": "pruned", "synced_unix": NOW, "bytes": MB}]

    assert synced_session_ids(ledger, tmp_path / "audio") == {"pruned"}
