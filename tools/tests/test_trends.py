from datetime import date, datetime, timedelta
from zoneinfo import ZoneInfo

import pytest

from nocturne_pc.context import Location, derive_context
from nocturne_pc.db import connect
from nocturne_pc.nights import NIGHT_COLUMNS
from nocturne_pc.trends import clock_text, load_settings, parse_settings, store_settings, summary

TORONTO = Location(43.6532, -79.3832, "America/Toronto")
ZONE = ZoneInfo(TORONTO.timezone)
SOURCE = "pc:yamnet-float"
MONDAY = date(2026, 9, 7)

def clock(night_of, hour, minute=0):
    day = night_of if hour >= 12 else night_of + timedelta(days=1)
    return datetime(day.year, day.month, day.day, hour, minute, tzinfo=ZONE).timestamp()

def add_night(connection, night_of, bed=(23, 0), asleep=(23, 20), awake=(7, 0), up=(7, 15), session_id=None, **metrics):
    bedtime = clock(night_of, *bed)
    onset = clock(night_of, *asleep)
    wake = clock(night_of, *awake)
    up_unix = clock(night_of, *up)
    if session_id is None:
        session_id = f"{night_of:%Y%m%d}_{bed[0]:02d}{bed[1]:02d}"

    row = {name: 0 for name in NIGHT_COLUMNS}
    row.update({
        "session_id": session_id, "source": SOURCE, "rules_sha256": "",
        "bedtime_unix": bedtime, "up_unix": up_unix, "time_in_bed_min": (up_unix - bedtime) / 60,
        "onset_unix": onset, "onset_latency_min": (onset - bedtime) / 60,
        "wake_unix": wake, "lie_in_min": (up_unix - wake) / 60,
        "sleep_min": (wake - onset) / 60, "efficiency": (wake - onset) / (up_unix - bedtime),
        "midpoint_unix": (onset + wake) / 2,
        "snoring_peak_db": None, "snoring_worst_hour_unix": None, "breathing_pauses_per_hour": None,
        "first_alarm_unix": None, "last_alarm_unix": None, "snooze_span_min": None, "alarm_to_up_min": None,
        "noise_floor_db": None, "snoring_share": None,
    })
    row.update(metrics)

    connection.execute("INSERT INTO sessions (session_id, device_id, started_unix, ended_unix, segment_count, "
                       "ingested_unix) VALUES (?, 'aa', ?, ?, 1, 0)", [session_id, int(bedtime), int(up_unix)])
    values = []
    for name in NIGHT_COLUMNS:
        values.append(row[name])
    connection.execute(f"INSERT INTO nights ({', '.join(NIGHT_COLUMNS)}) VALUES ({', '.join(['?'] * len(values))})",
                       values)
    return session_id

def add_event(connection, session_id, class_name, start_unix, end_unix):
    connection.execute("INSERT INTO events VALUES (?, ?, ?, 0, 0, 0, 0, ?, ?, ?, 1, 0.9, 0.9)",
                       [session_id, SOURCE, class_name, start_unix, end_unix, int((end_unix - start_unix) * 1000)])

@pytest.fixture
def connection(tmp_path):
    database = connect(tmp_path / "nocturne.duckdb")
    store_settings(database, load_settings())
    yield database
    database.close()

def nights_from(first, count):
    days = []
    for index in range(count):
        days.append(first + timedelta(days=index))
    return days

def test_the_shipped_settings_load():
    settings = load_settings()

    assert settings.sleep_target_h == 8
    assert settings.predictor_min_nights == 14

@pytest.mark.parametrize("change, message", [
    ({"target": 8}, "unknown setting target"),
    ({"sleep_target_h": "eight"}, "must be a number"),
    ({"illness_factor": True}, "must be a number"),
    ({"illness_factor": -1}, "cannot be negative"),
    ({"predictor_min_nights": 2.5}, "whole number"),
    ({"sleep_target_h": 25}, "cannot exceed"),
])
def test_bad_settings_are_refused(change, message):
    config = load_settings()._asdict()
    config.update(change)

    with pytest.raises(ValueError, match=message):
        parse_settings(config)

def test_a_missing_setting_is_refused():
    config = load_settings()._asdict()
    del config["illness_factor"]

    with pytest.raises(ValueError, match="no illness_factor"):
        parse_settings(config)

def test_clock_times_are_local_minutes_from_noon_across_a_clock_change(connection):
    night_of = date(2026, 10, 31)
    add_night(connection, night_of, bed=(23, 30), asleep=(23, 45), awake=(7, 0), up=(7, 30))
    derive_context(connection, TORONTO)

    row = connection.execute("SELECT bedtime_clock_min, wake_clock_min, utc_offset_min, clock_shift_min, "
                             "week_after_clock_change FROM trend_daylight").fetchall()[0]

    assert row == (pytest.approx(690), pytest.approx(1140), -240, -60, True)
    assert connection.execute("SELECT up_clock_min FROM trend_nights").fetchall() == [(pytest.approx(1170),)]
    assert clock_text(row[0]) == "23:30"
    assert clock_text(row[1]) == "07:00"

def test_the_longest_session_is_the_night_and_naps_are_counted(connection):
    add_night(connection, MONDAY, bed=(15, 0), asleep=(15, 10), awake=(15, 50), up=(16, 0))
    main = add_night(connection, MONDAY)
    derive_context(connection, TORONTO)

    assert connection.execute("SELECT session_id, sessions FROM trend_nights").fetchall() == [(main, 2)]

def test_rolling_means_cover_seven_calendar_nights(connection):
    for index, night_of in enumerate(nights_from(MONDAY, 9)):
        if index != 3:
            add_night(connection, night_of, sleep_min=400 + 10 * index)
    derive_context(connection, TORONTO)

    rows = connection.execute("SELECT night_of, mean_7, nights_7 FROM trend_rolling WHERE metric = 'sleep_min' "
                              "ORDER BY night_of").fetchall()

    assert rows[-1] == (MONDAY + timedelta(days=8), pytest.approx((420 + 440 + 450 + 460 + 470 + 480) / 6), 6)
    assert rows[0][2] == 1

def test_regularity_is_100_for_the_same_night_and_falls_with_a_shift(connection):
    add_night(connection, MONDAY, asleep=(23, 0), awake=(7, 0))
    add_night(connection, MONDAY + timedelta(days=1), asleep=(23, 0), awake=(7, 0))
    add_night(connection, MONDAY + timedelta(days=2), asleep=(1, 0), awake=(9, 0), bed=(0, 30), up=(9, 15))
    derive_context(connection, TORONTO)

    rows = connection.execute("SELECT night_of, sri, sri_7 FROM trend_consistency ORDER BY night_of").fetchall()

    assert rows[0][1] is None
    assert rows[1][1] == pytest.approx(100)
    assert rows[2][1] == pytest.approx(100 - 200 * 240 / 1440)
    assert rows[2][2] == pytest.approx((100 + 100 - 200 * 240 / 1440) / 2)

def test_free_nights_are_compared_with_working_ones(connection):
    for night_of in nights_from(MONDAY, 7):
        if night_of.isoweekday() in (5, 6):
            add_night(connection, night_of, asleep=(0, 20), awake=(8, 0))
        else:
            add_night(connection, night_of, asleep=(23, 20), awake=(7, 0))
    derive_context(connection, TORONTO)

    row = connection.execute("SELECT work_nights, free_nights, free_minus_work FROM trend_weekend "
                             "WHERE metric = 'midpoint_clock_min'").fetchall()[0]

    assert row == (5, 2, pytest.approx(60))
    assert connection.execute("SELECT nights FROM trend_weekday WHERE metric = 'sleep_min' AND weekday = 1"
                              ).fetchall() == [(1,)]

def test_sleep_debt_runs_over_fourteen_nights(connection):
    add_night(connection, MONDAY, sleep_min=420)
    add_night(connection, MONDAY + timedelta(days=1), sleep_min=360)
    add_night(connection, MONDAY + timedelta(days=2), sleep_min=None, onset_unix=None)
    add_night(connection, MONDAY + timedelta(days=20), sleep_min=540)
    derive_context(connection, TORONTO)

    rows = connection.execute("SELECT shortfall_min, debt_14_min, nights_14 FROM trend_sleep_debt "
                              "ORDER BY night_of").fetchall()

    assert rows == [(60, 60, 1), (120, 180, 2), (-60, -60, 1)]

def test_coughing_well_above_the_usual_is_flagged(connection):
    for night_of in nights_from(MONDAY, 7):
        add_night(connection, night_of, coughs=1)
    add_night(connection, MONDAY + timedelta(days=7), coughs=2)
    add_night(connection, MONDAY + timedelta(days=8), coughs=5)
    derive_context(connection, TORONTO)

    rows = connection.execute("SELECT coughs, baseline_nights, cough_flag, illness_flag FROM trend_illness "
                              "ORDER BY night_of").fetchall()

    assert rows[6] == (1, 6, False, False)
    assert rows[7] == (2, 7, False, False)
    assert rows[8] == (5, 8, True, True)

def test_too_few_nights_for_a_baseline_flag_nothing(connection):
    add_night(connection, MONDAY, coughs=0)
    add_night(connection, MONDAY + timedelta(days=1), coughs=20)
    derive_context(connection, TORONTO)

    assert connection.execute("SELECT bool_or(illness_flag) FROM trend_illness").fetchall() == [(False,)]

def test_predictors_are_ranked_once_there_are_enough_nights(connection):
    for index, night_of in enumerate(nights_from(MONDAY, 13)):
        add_night(connection, night_of, bed=(22, 10 * (index % 6)), efficiency=0.95 - 0.01 * (index % 6))
    derive_context(connection, TORONTO)

    assert connection.execute("SELECT COUNT(*) FROM trend_predictors").fetchall() == [(0,)]

    add_night(connection, MONDAY + timedelta(days=13), bed=(22, 0), efficiency=0.95)
    derive_context(connection, TORONTO)
    rows = connection.execute("SELECT predictor, nights, correlation FROM trend_predictors "
                              "WHERE outcome = 'efficiency' AND rank = 1").fetchall()

    assert rows == [("bedtime_clock_min", 14, pytest.approx(-1))]

def test_records_pick_the_extreme_night(connection):
    add_night(connection, MONDAY, bed=(22, 0), coughs=4)
    add_night(connection, MONDAY + timedelta(days=1), bed=(21, 30), coughs=4)
    add_night(connection, MONDAY + timedelta(days=2), bed=(0, 15), asleep=(0, 30), coughs=1)
    derive_context(connection, TORONTO)

    records = {}
    for record, value, night_of in connection.execute("SELECT record, value, night_of FROM trend_records").fetchall():
        records[record] = (value, night_of)

    assert records["earliest bedtime"] == (pytest.approx(570), MONDAY + timedelta(days=1))
    assert records["latest bedtime"] == (pytest.approx(735), MONDAY + timedelta(days=2))
    assert records["most coughs"] == (4, MONDAY)

def test_the_heatmap_splits_events_at_the_hour(connection):
    session_id = add_night(connection, MONDAY)
    add_event(connection, session_id, "movement", clock(MONDAY, 23, 50), clock(MONDAY, 0, 20))
    add_event(connection, session_id, "movement", clock(MONDAY, 0, 30), clock(MONDAY, 0, 35))
    derive_context(connection, TORONTO)

    rows = connection.execute("SELECT hour, clock_hour, class, minutes FROM trend_heatmap ORDER BY hour").fetchall()

    assert rows == [(11, 23, "movement", pytest.approx(10)), (12, 0, "movement", pytest.approx(25))]

def test_years_sum_their_nights(connection):
    add_night(connection, date(2026, 12, 30), coughs=2, snoring_min=30)
    add_night(connection, date(2026, 12, 31), coughs=1, snoring_min=60)
    add_night(connection, date(2027, 1, 1), coughs=5)
    derive_context(connection, TORONTO)

    rows = connection.execute("SELECT year, nights, coughs, snoring_h FROM trend_years ORDER BY year").fetchall()

    assert rows == [(2026, 2, 3, pytest.approx(1.5)), (2027, 1, 5, pytest.approx(0))]

def test_the_summary_reports_the_latest_week(connection):
    for night_of in nights_from(MONDAY, 3):
        add_night(connection, night_of)
    derive_context(connection, TORONTO)

    text = summary(connection, SOURCE)

    assert "Wed 2026-09-09" in text
    assert "sleep 7.7 h" in text
    assert "bedtime 23:00" in text
    assert "regularity 100" in text
    assert summary(connection, "device:none") is None
