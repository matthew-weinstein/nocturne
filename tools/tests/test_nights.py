import tomllib

import pytest

from nocturne_pc.db import connect
from nocturne_pc.nights import (
    RULES_PATH,
    Span,
    bathroom_trips,
    breathing_pauses,
    derive_night,
    full_scale_db_spl,
    join_spans,
    load_settings,
    nights_to_derive,
    parse_settings,
    restless_bouts,
    settings_digest,
    sleep_onset,
    wake_time,
    worst_hour,
)
from nocturne_pc.teacher import SOURCE, WINDOW_FRAMES

SESSION_ID = "20261108_0410"
BEDTIME = 1794128400
HOUR = 3600
MINUTE = 60
NOW = 1794200000

def settings_config():
    with open(RULES_PATH, "rb") as file:
        return tomllib.load(file)

def test_full_scale_is_about_78_db_at_shift_9_and_exact_at_16():
    assert full_scale_db_spl(9) == pytest.approx(77.86, abs=0.01)
    assert full_scale_db_spl(16) == 120

def test_spans_join_across_gaps_up_to_the_limit():
    spans = [Span(0, 5), Span(20, 25), Span(36, 40)]

    assert join_spans(spans, 15) == [Span(0, 40)]
    assert join_spans(spans, 14) == [Span(0, 5), Span(20, 40)]
    assert join_spans(spans, 10) == [Span(0, 5), Span(20, 25), Span(36, 40)]

def test_onset_is_the_first_quiet_stretch_from_bedtime_or_an_activitys_end():
    activity = [Span(100, 110), Span(600, 610), Span(2000, 2010)]

    assert sleep_onset(0, 10000, activity, 1200) == 610
    assert sleep_onset(0, 10000, [], 1200) == 0
    assert sleep_onset(0, 1500, activity, 1200) is None

def test_wake_is_the_first_alarm_after_onset():
    activity = [Span(9000, 9500)]
    alarms = [Span(500, 510), Span(8000, 8010)]

    assert wake_time(1000, 9600, activity, alarms, 300) == 8000

def test_without_an_alarm_wake_is_the_activity_running_into_up():
    activity = [Span(5000, 5010), Span(9000, 9100), Span(9300, 9400)]

    assert wake_time(1000, 9500, activity, [], 300) == 9000
    assert wake_time(1000, 9800, activity, [], 400) == 9000
    assert wake_time(1000, 9800, activity, [], 300) == 9800

def test_restless_bouts_join_short_movement_and_drop_lone_twitches():
    movement = [Span(100, 105), Span(120, 125), Span(150, 160), Span(1000, 1010), Span(5000, 5100)]

    bouts = restless_bouts(movement, 0, 5050, 30, 30)

    assert bouts == [Span(100, 160), Span(5000, 5050)]

def test_a_snore_then_quiet_then_a_gasp_is_a_breathing_pause():
    snore = Span(100, 102)
    gasp = Span(117, 118)
    others = [snore, gasp]

    assert breathing_pauses([snore], others, [gasp.start], 0, 1000, 10, 120) == 1
    assert breathing_pauses([snore], others, [gasp.start], 0, 50, 10, 120) == 0
    assert breathing_pauses([snore], others, [gasp.start], 0, 1000, 20, 120) == 0

def test_a_sound_in_the_quiet_or_a_non_gasp_after_it_is_not_a_pause():
    snore = Span(100, 102)
    gasp = Span(117, 118)
    movement = Span(110, 111)

    assert breathing_pauses([snore], [snore, movement, gasp], [gasp.start], 0, 1000, 10, 120) == 0
    assert breathing_pauses([snore], [snore, Span(117, 118)], [], 0, 1000, 10, 120) == 0
    assert breathing_pauses([snore], [snore, Span(99, 130), gasp], [gasp.start], 0, 1000, 10, 120) == 0

def test_a_snort_window_ends_a_pause():
    snore = Span(100, 102)
    snort = Span(115, 115.96)

    assert breathing_pauses([snore], [snore, snort], [snort.start], 0, 1000, 10, 120) == 1

def test_a_bathroom_trip_is_steps_then_water_within_the_window():
    steps = [Span(100, 101), Span(110, 111), Span(5000, 5001)]
    water = [Span(400, 430), Span(6000, 6030)]

    assert bathroom_trips(steps, water, 0, 10000, 600) == 1
    assert bathroom_trips(steps, water, 0, 10000, 1000) == 2
    assert bathroom_trips(steps, water, 200, 10000, 600) == 0

def test_the_worst_hour_counts_time_on_each_side_of_the_hour():
    spans = [Span(HOUR - 100, HOUR + 400), Span(3 * HOUR, 3 * HOUR + 300)]

    assert worst_hour(spans) == HOUR
    assert worst_hour([Span(10, 20), Span(HOUR + 10, HOUR + 20)]) == 0
    assert worst_hour([]) is None

def test_the_shipped_rules_load():
    settings = load_settings()

    assert "movement" in settings.onset_activity
    assert settings.min_session_min == 10

@pytest.mark.parametrize("change, message", [
    ({"bedtime": 1}, "unknown setting bedtime"),
    ({"onset_activity": ["moving"]}, "not a class"),
    ({"onset_activity": "movement"}, "must be a list"),
    ({"alarm_gap_s": "long"}, "must be a number"),
    ({"alarm_gap_s": -1}, "cannot be negative"),
    ({"pause_min_s": 200}, "cannot exceed"),
])
def test_bad_rules_are_refused(change, message):
    config = settings_config()
    config.update(change)

    with pytest.raises(ValueError, match=message):
        parse_settings(config)

def test_a_missing_setting_is_refused():
    config = settings_config()
    del config["snort_score"]

    with pytest.raises(ValueError, match="no snort_score"):
        parse_settings(config)

@pytest.fixture
def connection(tmp_path):
    database = connect(tmp_path / "nocturne.duckdb")
    yield database
    database.close()

def add_session(connection, hours, session_id=SESSION_ID, derived_unix=NOW):
    up = BEDTIME + hours * HOUR
    connection.execute("INSERT INTO sessions (session_id, device_id, started_unix, ended_unix, segment_count, "
                       "sample_shift, muted_ms, ingested_unix) VALUES (?, 'aa', ?, ?, 1, 9, 3000, 0)",
                       [session_id, BEDTIME, up])
    connection.execute("INSERT INTO segments VALUES (?, 0, ?, 1, 1, false, false)",
                       [session_id, int(hours * HOUR / 0.02)])
    connection.execute("INSERT INTO evented VALUES (?, ?, 'digest', 0, 0, ?)", [session_id, SOURCE, derived_unix])

def add_event(connection, class_name, start, seconds):
    connection.execute("INSERT INTO events VALUES (?, ?, ?, 0, 0, 0, 0, ?, ?, ?, 1, 0.9, 0.9)",
                       [SESSION_ID, SOURCE, class_name, BEDTIME + start, BEDTIME + start + seconds,
                        int(seconds * 1000)])

def add_level(connection, start, rms_dbfs, peak_dbfs, top_class=None):
    first_frame = int(start / 0.02)
    connection.execute("INSERT INTO levels VALUES (?, 0, ?, ?, ?)", [SESSION_ID, first_frame, rms_dbfs, peak_dbfs])
    if top_class is not None:
        connection.execute("INSERT INTO teacher VALUES (?, 0, ?, 1, ?, 0.8)", [SESSION_ID, first_frame, top_class])

def test_a_night_is_derived_from_its_events_and_levels(connection):
    add_session(connection, 8)
    add_event(connection, "movement", 5 * MINUTE, 10)
    add_event(connection, "speech", 2 * HOUR, 3)
    add_event(connection, "snoring", 3 * HOUR, 120)
    add_event(connection, "cough", 4 * HOUR, 1)
    add_event(connection, "alarm", 7 * HOUR + 50 * MINUTE, 20)
    add_event(connection, "alarm", 7 * HOUR + 55 * MINUTE, 20)
    for index in range(10):
        add_level(connection, 3 * HOUR + index * WINDOW_FRAMES * 0.02, -40.0, -20.0)
    add_level(connection, 6 * HOUR, -30.0, -2.0, top_class="Slam")
    settings = load_settings()

    metrics = derive_night(connection, SESSION_ID, SOURCE, settings, "rules", NOW, NOW)

    assert metrics["onset_unix"] == BEDTIME + 5 * MINUTE + 10
    assert metrics["onset_latency_min"] == pytest.approx(5 + 10 / 60)
    assert metrics["wake_unix"] == BEDTIME + 7 * HOUR + 50 * MINUTE
    assert metrics["lie_in_min"] == pytest.approx(10)
    assert metrics["time_in_bed_min"] == pytest.approx(480)
    assert metrics["sleep_min"] == pytest.approx(470 - 5 - 10 / 60)
    assert metrics["sleep_talk"] == 1
    assert metrics["snoring_min"] == pytest.approx(2)
    assert metrics["snoring_worst_hour_unix"] == (BEDTIME + 3 * HOUR) // HOUR * HOUR
    assert metrics["snoring_peak_db"] == pytest.approx(-20 + full_scale_db_spl(9))
    assert metrics["coughs"] == 1
    assert metrics["alarms"] == 2
    assert metrics["snooze_span_min"] == pytest.approx(5)
    assert metrics["alarm_to_up_min"] == pytest.approx(5)
    assert metrics["noise_floor_db"] == pytest.approx(-40 + full_scale_db_spl(9))
    assert metrics["loud_moments"] == 1
    assert metrics["muted_min"] == pytest.approx(0.05)
    assert metrics["missing_min"] == pytest.approx(0, abs=1e-6)

    stored = connection.execute("SELECT top_class, peak_db FROM loud_moments").fetchall()
    assert stored == [("Slam", pytest.approx(-2 + full_scale_db_spl(9), abs=1e-4))]
    assert connection.execute("SELECT sleep_min FROM nights").fetchall() == [(pytest.approx(metrics["sleep_min"]),)]

def test_brief_movement_does_not_hold_off_onset(connection):
    add_session(connection, 2)
    for minute in range(10, 60, 4):
        add_event(connection, "movement", minute * MINUTE, 0.96)
    add_event(connection, "movement", 5 * MINUTE, 3)

    metrics = derive_night(connection, SESSION_ID, SOURCE, load_settings(), "rules", NOW, NOW)

    assert metrics["onset_unix"] == BEDTIME + 5 * MINUTE + 3

def test_a_night_with_no_quiet_stretch_has_no_sleep_estimates(connection):
    add_session(connection, 0.5)
    for minute in range(0, 30, 5):
        add_event(connection, "movement", minute * MINUTE, 5)

    metrics = derive_night(connection, SESSION_ID, SOURCE, load_settings(), "rules", NOW, NOW)

    assert metrics["onset_unix"] is None
    assert metrics["sleep_min"] is None
    assert metrics["efficiency"] is None
    assert metrics["sleep_talk"] is None
    assert metrics["time_in_bed_min"] == pytest.approx(30)

def test_nights_are_derived_again_when_events_or_rules_change_and_short_ones_never(connection):
    add_session(connection, 8)
    add_session(connection, 0.1, session_id="20261108_0300")
    settings = load_settings()
    digest = settings_digest(settings)

    assert nights_to_derive(connection, settings, digest, force=False) == [(SESSION_ID, SOURCE, NOW)]

    derive_night(connection, SESSION_ID, SOURCE, settings, digest, NOW, NOW)
    derive_night(connection, SESSION_ID, SOURCE, settings, digest, NOW, NOW)

    assert connection.execute("SELECT COUNT(*) FROM nights").fetchone() == (1,)
    assert nights_to_derive(connection, settings, digest, force=False) == []
    assert nights_to_derive(connection, settings, digest, force=True) == [(SESSION_ID, SOURCE, NOW)]
    assert nights_to_derive(connection, settings, "other", force=False) != []

    connection.execute("UPDATE evented SET derived_unix = ?", [NOW + 60])
    assert nights_to_derive(connection, settings, digest, force=False) == [(SESSION_ID, SOURCE, NOW + 60)]
