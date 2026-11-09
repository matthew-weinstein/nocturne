from datetime import date, datetime, timedelta, timezone
from urllib.parse import parse_qs, urlparse
from zoneinfo import ZoneInfo

import pytest

from nocturne_pc.context import (
    Location,
    date_runs,
    derive_context,
    load_location,
    missing_dates,
    night_context,
    night_hours,
    parse_location,
    parse_weather,
    sun_event_unix,
    update_weather,
)
from nocturne_pc.db import connect

TORONTO = Location(43.6532, -79.3832, "America/Toronto")
HOUR = 3600
BEDTIME = 1794128400
UP = BEDTIME + 8 * HOUR
NOW = UP + 2 * HOUR

def local(unix, zone_name):
    return datetime.fromtimestamp(unix, ZoneInfo(zone_name))

def test_london_midsummer_matches_the_almanac():
    sunrise = local(sun_event_unix(date(2024, 6, 21), 51.5074, -0.1278, rising=True), "Europe/London")
    sunset = local(sun_event_unix(date(2024, 6, 21), 51.5074, -0.1278, rising=False), "Europe/London")

    assert abs(sunrise - sunrise.replace(hour=4, minute=43, second=0)) < timedelta(minutes=2)
    assert abs(sunset - sunset.replace(hour=21, minute=21, second=0)) < timedelta(minutes=2)

def test_the_equator_at_the_equinox_has_about_twelve_hours():
    sunrise = sun_event_unix(date(2026, 3, 20), 0, 0, rising=True)
    sunset = sun_event_unix(date(2026, 3, 20), 0, 0, rising=False)

    assert 12 * HOUR < sunset - sunrise < 12 * HOUR + 10 * 60

def test_polar_night_and_midnight_sun_have_no_sunrise():
    assert sun_event_unix(date(2026, 12, 21), 78.2, 15.6, rising=True) is None
    assert sun_event_unix(date(2026, 6, 21), 78.2, 15.6, rising=False) is None

def test_a_small_hours_bedtime_belongs_to_the_evening_before():
    bedtime = datetime(2026, 11, 8, 4, 10, tzinfo=ZoneInfo(TORONTO.timezone)).timestamp()

    row = night_context("s", bedtime, TORONTO)

    assert row["night_of"] == date(2026, 11, 7)
    assert row["weekday"] == 6
    assert row["free_day_after"]
    assert row["utc_offset_min"] == -300
    assert local(row["sunset_unix"], TORONTO.timezone).date() == date(2026, 11, 7)
    assert local(row["sunrise_unix"], TORONTO.timezone).date() == date(2026, 11, 8)

def test_a_sunday_night_has_a_working_day_after_and_summer_time_its_offset():
    bedtime = datetime(2026, 7, 12, 23, 30, tzinfo=ZoneInfo(TORONTO.timezone)).timestamp()

    row = night_context("s", bedtime, TORONTO)

    assert row["weekday"] == 7
    assert not row["free_day_after"]
    assert row["utc_offset_min"] == -240

def test_the_location_file_loads(tmp_path):
    path = tmp_path / "location.toml"
    path.write_text('latitude = 43.65\nlongitude = -79.38\ntimezone = "America/Toronto"\n')

    assert load_location(path) == Location(43.65, -79.38, "America/Toronto")

def test_a_missing_location_file_says_what_to_write(tmp_path):
    with pytest.raises(ValueError, match="latitude, longitude and timezone"):
        load_location(tmp_path / "location.toml")

@pytest.mark.parametrize("change, message", [
    ({"city": "Toronto"}, "unknown setting city"),
    ({"latitude": 91}, "within"),
    ({"longitude": "west"}, "must be a number"),
    ({"latitude": True}, "must be a number"),
    ({"timezone": "Toronto"}, "not an IANA zone"),
])
def test_bad_locations_are_refused(change, message):
    config = {"latitude": 43.65, "longitude": -79.38, "timezone": "America/Toronto"}
    config.update(change)

    with pytest.raises(ValueError, match=message):
        parse_location(config)

def test_a_missing_location_setting_is_refused():
    with pytest.raises(ValueError, match="no timezone"):
        parse_location({"latitude": 43.65, "longitude": -79.38})

def test_a_night_covers_every_hour_it_touches():
    assert night_hours(10 * HOUR + 5, 12 * HOUR + 1) == [10 * HOUR, 11 * HOUR, 12 * HOUR, 13 * HOUR]
    assert night_hours(10 * HOUR, 11 * HOUR) == [10 * HOUR, 11 * HOUR]

def test_dates_group_into_consecutive_runs():
    dates = [date(2026, 11, 1), date(2026, 11, 2), date(2026, 11, 3), date(2026, 11, 7)]

    assert date_runs(dates) == [(date(2026, 11, 1), date(2026, 11, 3)), (date(2026, 11, 7), date(2026, 11, 7))]
    assert date_runs([]) == []

def body(hours, temperature=5.0):
    return {"hourly": {
        "time": hours,
        "temperature_2m": [temperature] * len(hours),
        "relative_humidity_2m": [80] * len(hours),
        "pressure_msl": [1010.0 + index for index in range(len(hours))],
        "precipitation": [0.5] * len(hours),
    }}

def test_future_and_empty_hours_are_not_kept():
    answer = body([0, HOUR, 2 * HOUR])
    answer["hourly"]["temperature_2m"][1] = None
    answer["hourly"]["relative_humidity_2m"][1] = None
    answer["hourly"]["pressure_msl"][1] = None
    answer["hourly"]["precipitation"][1] = None

    rows = parse_weather(answer, now_unix=HOUR)

    assert rows == [(0, 5.0, 80, 1010.0, 0.5)]

def test_an_answer_with_a_short_variable_is_refused():
    answer = body([0, HOUR])
    answer["hourly"]["precipitation"] = [0.0]

    with pytest.raises(RuntimeError, match="precipitation"):
        parse_weather(answer, now_unix=HOUR)

@pytest.fixture
def connection(tmp_path):
    database = connect(tmp_path / "nocturne.duckdb")
    yield database
    database.close()

def add_night(connection, session_id, bedtime, up):
    connection.execute("INSERT INTO sessions (session_id, device_id, started_unix, ended_unix, segment_count, "
                       "ingested_unix) VALUES (?, 'aa', ?, ?, 1, 0)", [session_id, bedtime, up])
    connection.execute("INSERT INTO nights (session_id, source, bedtime_unix, up_unix, time_in_bed_min, wake_unix, "
                       "lie_in_min, snoring_min, snoring_per_hour, coughs, sneezes, alarms, loud_moments, outage_min, "
                       "muted_min, missing_min, rules_sha256, events_derived_unix, derived_unix) "
                       "VALUES (?, 'pc', ?, ?, 0, ?, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, '', 0, 0)",
                       [session_id, bedtime, up, up])

def answer_from_url(asked):
    def fetch(url):
        query = parse_qs(urlparse(url).query)
        asked.append(query)
        first = datetime.fromisoformat(query["start_date"][0]).replace(tzinfo=timezone.utc).timestamp()
        last = datetime.fromisoformat(query["end_date"][0]).replace(tzinfo=timezone.utc).timestamp()
        hours = list(range(int(first), int(last) + 24 * HOUR, HOUR))
        return body(hours)
    return fetch

def test_weather_is_fetched_once_for_the_nights_hours_at_a_rounded_location(connection):
    add_night(connection, "20261108_0410", BEDTIME, UP)
    derive_context(connection, TORONTO)
    asked = []

    stored = update_weather(connection, TORONTO, NOW, fetch=answer_from_url(asked))

    assert len(asked) == 1
    assert asked[0]["latitude"] == ["43.65"]
    assert asked[0]["longitude"] == ["-79.38"]
    assert asked[0]["start_date"] == asked[0]["end_date"] == ["2026-11-08"]
    assert stored == 20

    assert update_weather(connection, TORONTO, NOW, fetch=answer_from_url(asked)) == 0
    assert len(asked) == 1

def test_night_weather_averages_the_nights_hours(connection):
    add_night(connection, "20261108_0410", BEDTIME, UP)
    derive_context(connection, TORONTO)
    update_weather(connection, TORONTO, NOW, fetch=answer_from_url([]))

    hours, temperature, change, precipitation = connection.execute(
        "SELECT weather_hours, temperature_c, pressure_change_hpa, precipitation_mm FROM night_weather").fetchall()[0]

    assert hours == 9
    assert temperature == pytest.approx(5)
    assert change == pytest.approx(8)
    assert precipitation == pytest.approx(0.5 * 8)

def test_context_is_rebuilt_for_every_night(connection):
    add_night(connection, "20261108_0410", BEDTIME, UP)
    derive_context(connection, TORONTO)
    add_night(connection, "20261109_2300", BEDTIME + 43 * HOUR, BEDTIME + 51 * HOUR)

    rows = derive_context(connection, TORONTO)

    assert len(rows) == 2
    assert connection.execute("SELECT COUNT(*) FROM night_context").fetchall() == [(2,)]

def test_separate_nights_are_fetched_separately(connection):
    add_night(connection, "a", BEDTIME, UP)
    add_night(connection, "b", BEDTIME + 10 * 24 * HOUR, UP + 10 * 24 * HOUR)

    assert len(date_runs(missing_dates(connection, UP + 11 * 24 * HOUR))) == 2
