import time

import numpy as np
import pytest

from nocturne_pc.db import connect
from nocturne_pc.events import (
    RULES_PATH,
    Event,
    Rule,
    derive_session,
    load_rules,
    parse_rules,
    rules_digest,
    segment_clock,
    sessions_to_derive,
    store_events,
    stretch_events,
)
from nocturne_pc.teacher import SOURCE, WINDOW_FRAMES, insert_columns

SESSION_ID = "20261017_0248"
STARTED = 1792205292
NOW = 1792300000
RULE = Rule(on=0.5, off=0.3, min_ms=0, merge_ms=0)

def windows_from(scores, start=0):
    windows = []
    for index, score in enumerate(scores):
        first = start + index * WINDOW_FRAMES
        windows.append((first, first + WINDOW_FRAMES, score))
    return windows

def spans(events):
    result = []
    for event in events:
        result.append((event.start // WINDOW_FRAMES, event.end // WINDOW_FRAMES))
    return result

def test_a_run_above_off_is_an_event_only_if_it_reaches_on():
    windows = windows_from([0.1, 0.35, 0.6, 0.4, 0.2, 0.4, 0.45, 0.1, 0.9])

    events = stretch_events(windows, RULE)

    assert spans(events) == [(1, 4), (8, 9)]
    assert events[0] == Event(48, 192, 3, 0.6, pytest.approx(1.35))

def test_a_missing_window_splits_a_run_and_merging_rejoins_it():
    windows = windows_from([0.6, 0.6]) + windows_from([0.6], start=3 * WINDOW_FRAMES)

    assert spans(stretch_events(windows, RULE)) == [(0, 2), (3, 4)]
    assert spans(stretch_events(windows, RULE._replace(merge_ms=960))) == [(0, 4)]
    assert spans(stretch_events(windows, RULE._replace(merge_ms=940))) == [(0, 2), (3, 4)]

def test_events_merge_before_short_ones_are_dropped():
    windows = windows_from([0.6, 0.1, 0.6, 0.1, 0.1, 0.1, 0.6])
    rule = RULE._replace(min_ms=2880, merge_ms=1000)

    events = stretch_events(windows, rule)

    assert spans(events) == [(0, 3)]
    assert events[0].windows == 2

def test_the_shipped_rules_load_and_name_only_real_classes():
    rules = load_rules(RULES_PATH)

    assert "other" not in rules
    assert rules["cough"] == Rule(on=0.3, off=0.2, min_ms=0, merge_ms=1920)
    assert rules["silence"].min_ms == 10000

@pytest.mark.parametrize("config, message", [
    ({"classes": {"coughs": {}}, "default": {"on": 0.5, "off": 0.3, "min_ms": 0, "merge_ms": 0}}, "not a class"),
    ({"classes": {"cough": {"on": 0.5, "off": 0.3, "min_ms": 0}}}, "no merge_ms"),
    ({"classes": {"cough": {"on": 0.2, "off": 0.3, "min_ms": 0, "merge_ms": 0}}}, "off <= on"),
    ({"classes": {"cough": {"on": 0.5, "off": 0.3, "min_ms": 0, "merge_ms": 0, "gap": 1}}}, "unknown setting gap"),
    ({"classes": {"cough": {"on": "high", "off": 0.3, "min_ms": 0, "merge_ms": 0}}}, "must be a number"),
    ({"class": {}}, "unknown section"),
])
def test_bad_rules_are_refused(config, message):
    with pytest.raises(ValueError, match=message):
        parse_rules(config)

def test_the_digest_follows_the_rules_not_their_order():
    first = {"cough": RULE, "speech": RULE._replace(on=0.6)}
    second = {"speech": RULE._replace(on=0.6), "cough": RULE}

    assert rules_digest(first) == rules_digest(second)
    assert rules_digest(first) != rules_digest({"cough": RULE})

def test_a_segment_after_a_missing_one_has_no_clock_until_the_next_resume():
    segment_rows = [(0, 100, False), (1, 50, False), (3, 100, False), (4, 100, False), (5, 100, False)]
    runs = [(0, 0, 1000), (1, 5, 5000)]

    clock = segment_clock(segment_rows, runs)

    assert clock == {0: 1000, 1: 1002.0, 3: None, 4: None, 5: 5000}

@pytest.fixture
def connection(tmp_path):
    database = connect(tmp_path / "nocturne.duckdb")
    yield database
    database.close()

def add_session(connection, segment_frames, resumes=(), scored_unix=NOW):
    connection.execute("INSERT INTO sessions (session_id, device_id, started_unix, ended_unix, segment_count, "
                       "ingested_unix) VALUES (?, 'aa', ?, ?, ?, 0)",
                       [SESSION_ID, STARTED, STARTED + 3600, len(segment_frames)])
    for segment, frames in enumerate(segment_frames):
        connection.execute("INSERT INTO segments VALUES (?, ?, ?, 1, 1, false, false)", [SESSION_ID, segment, frames])
    for resume, (segment, anchor_unix) in enumerate(resumes, start=1):
        connection.execute("INSERT INTO resumes VALUES (?, ?, ?, ?)", [SESSION_ID, resume, segment, anchor_unix])
    connection.execute("INSERT INTO scored VALUES (?, ?, 0, 0, ?)", [SESSION_ID, SOURCE, scored_unix])

def add_scores(connection, class_name, segment, scores, first_frame=0):
    for index, score in enumerate(scores):
        connection.execute("INSERT INTO windows VALUES (?, ?, ?, ?, ?, ?)",
                           [SESSION_ID, SOURCE, segment, first_frame + index * WINDOW_FRAMES, class_name, score])

def test_an_event_runs_across_a_segment_close_with_clock_times(connection):
    add_session(connection, [100, 100])
    add_scores(connection, "cough", 0, [0.1, 0.6, 0.6])
    add_scores(connection, "cough", 1, [0.6, 0.1], first_frame=44)

    rows = derive_session(connection, SESSION_ID, SOURCE, {"cough": RULE})

    assert rows == [(SESSION_ID, SOURCE, "cough", 0, 48, 1, 92, STARTED + 0.96, pytest.approx(STARTED + 3.84, abs=1e-6),
                     2880, 3, pytest.approx(0.6), pytest.approx(0.6))]

def test_the_last_window_of_a_stretch_ends_with_its_audio(connection):
    add_session(connection, [60])
    add_scores(connection, "cough", 0, [0.1, 0.6])

    rows = derive_session(connection, SESSION_ID, SOURCE, {"cough": RULE})

    assert rows[0][3:7] == (0, 48, 0, 60)
    assert rows[0][9] == 240

def test_a_resume_is_never_merged_across(connection):
    add_session(connection, [96, 96], resumes=[(1, STARTED + 900)])
    add_scores(connection, "cough", 0, [0.1, 0.6])
    add_scores(connection, "cough", 1, [0.6, 0.1])

    rows = derive_session(connection, SESSION_ID, SOURCE, {"cough": RULE._replace(merge_ms=60000)})

    assert [(row[3], row[4], row[7]) for row in rows] == [(0, 48, STARTED + 0.96), (1, 0, STARTED + 900)]

def test_a_class_without_a_rule_has_no_events(connection):
    add_session(connection, [100])
    add_scores(connection, "cough", 0, [0.9])
    add_scores(connection, "other", 0, [0.9])

    rows = derive_session(connection, SESSION_ID, SOURCE, {"cough": RULE})

    assert [row[2] for row in rows] == ["cough"]

def test_sessions_are_derived_again_when_scores_or_rules_change(connection):
    add_session(connection, [100])
    add_scores(connection, "cough", 0, [0.9])
    digest = rules_digest({"cough": RULE})

    assert sessions_to_derive(connection, digest, force=False) == [(SESSION_ID, SOURCE, NOW)]

    rows = derive_session(connection, SESSION_ID, SOURCE, {"cough": RULE})
    store_events(connection, SESSION_ID, SOURCE, rows, digest, NOW, NOW)
    store_events(connection, SESSION_ID, SOURCE, rows, digest, NOW, NOW)

    assert connection.execute("SELECT COUNT(*) FROM events").fetchone() == (1,)
    assert connection.execute("SELECT events FROM evented").fetchall() == [(1,)]
    assert sessions_to_derive(connection, digest, force=False) == []
    assert sessions_to_derive(connection, digest, force=True) == [(SESSION_ID, SOURCE, NOW)]
    assert sessions_to_derive(connection, rules_digest({"cough": RULE._replace(on=0.4)}), force=False) != []

    connection.execute("UPDATE scored SET scored_unix = ?", [NOW + 60])
    assert sessions_to_derive(connection, digest, force=False) == [(SESSION_ID, SOURCE, NOW + 60)]

def test_a_night_derives_in_seconds(connection):
    add_session(connection, [15000] * 96)
    rules = load_rules()
    columns = {"session_id": [], "source": [], "segment": [], "first_frame": [], "class": [], "score": []}
    for segment in range(96):
        for index in range(15000 // WINDOW_FRAMES + 1):
            for class_name in rules:
                columns["session_id"].append(SESSION_ID)
                columns["source"].append(SOURCE)
                columns["segment"].append(segment)
                columns["first_frame"].append(index * WINDOW_FRAMES)
                columns["class"].append(class_name)
                columns["score"].append(((segment * 7 + index * 13 + len(class_name)) % 100) / 100)
    arrays = {}
    for name, values in columns.items():
        arrays[name] = np.array(values)
    insert_columns(connection, "windows", arrays)

    started = time.perf_counter()
    events = derive_session(connection, SESSION_ID, SOURCE, rules)
    store_events(connection, SESSION_ID, SOURCE, events, rules_digest(rules), NOW, NOW)

    assert len(events) > 1000
    assert time.perf_counter() - started < 10

def test_an_event_after_a_missing_segment_is_stored_without_clock_times(connection):
    add_session(connection, [100, 100])
    connection.execute("DELETE FROM segments WHERE segment = 0")
    add_scores(connection, "cough", 1, [0.9])

    rows = derive_session(connection, SESSION_ID, SOURCE, {"cough": RULE})
    store_events(connection, SESSION_ID, SOURCE, rows, "digest", NOW, NOW)

    assert connection.execute("SELECT start_segment, start_unix, end_unix, peak_score FROM events").fetchall() == [
        (1, None, None, pytest.approx(0.9))]
