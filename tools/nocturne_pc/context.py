"""Night context: calendar, daylight and weather (design doc 9.14, §22).

    cd tools
    python -m nocturne_pc.context
    python -m nocturne_pc.context --no-weather

Needs location.toml at the data root (never in the repo):

    latitude = 43.65
    longitude = -79.38
    timezone = "America/Toronto"

Every night in nights gets a night_context row, all rebuilt each run: the
night's date (the local date of bedtime less 12 hours, so 04:10 belongs to
the evening before), its weekday, whether a free day follows, the UTC offset
at bedtime, that evening's sunset and the next morning's sunrise. Sunrise and
sunset are computed here (NOAA's solar equations), not fetched.

Weather is hourly from Open-Meteo's historical forecast API (free, no key),
asked only for the hours of nights not yet in weather, at the location
rounded to two decimals. Hours still to come are not stored.
"""

import argparse
import json
import math
import time
import tomllib
import urllib.error
import urllib.parse
import urllib.request
from collections import namedtuple
from datetime import datetime, timedelta, timezone
from pathlib import Path
from zoneinfo import ZoneInfo, ZoneInfoNotFoundError

from nocturne_pc import data_root
from nocturne_pc.db import connect

LOCATION_NAME = "location.toml"
LOCATION_KEYS = ("latitude", "longitude", "timezone")
LOCATION_DIGITS = 2
CONTEXT_COLUMNS = ("session_id", "night_of", "weekday", "free_day_after", "utc_offset_min", "sunset_unix", "sunrise_unix")
WEATHER_URL = "https://historical-forecast-api.open-meteo.com/v1/forecast"
WEATHER_TIMEOUT_S = 30
WEATHER_VARIABLES = {
    "temperature_2m": "temperature_c",
    "relative_humidity_2m": "humidity_pct",
    "pressure_msl": "pressure_hpa",
    "precipitation": "precipitation_mm",
}
SECONDS_PER_HOUR = 3600
SECONDS_PER_DAY = 86400
NIGHT_SHIFT_S = 12 * SECONDS_PER_HOUR
FREE_DAY_EVES = (5, 6)
UNIX_EPOCH_JULIAN_DAY = 2440587.5
J2000_JULIAN_DAY = 2451545.0
DAYS_PER_CENTURY = 36525
SUN_ZENITH_DEG = 90.833
MINUTES_PER_DAY = 1440

Location = namedtuple("Location", LOCATION_KEYS)

def location_path():
    return data_root() / LOCATION_NAME

def load_location(path=None):
    if path is None:
        path = location_path()
    path = Path(path)
    if not path.exists():
        raise ValueError(f"no {path}: write latitude, longitude and timezone there (see python -m nocturne_pc.context -h)")

    with open(path, "rb") as file:
        return parse_location(tomllib.load(file))

def parse_location(config):
    for key in config:
        if key not in LOCATION_KEYS:
            raise ValueError(f"unknown setting {key}")
    for key in LOCATION_KEYS:
        if key not in config:
            raise ValueError(f"no {key}")

    for key, limit in (("latitude", 90), ("longitude", 180)):
        value = config[key]
        if isinstance(value, bool) or not isinstance(value, (int, float)):
            raise ValueError(f"{key} must be a number")
        if abs(value) > limit:
            raise ValueError(f"{key} must be within ±{limit}")

    try:
        ZoneInfo(config["timezone"])
    except (ZoneInfoNotFoundError, ValueError, TypeError):
        raise ValueError(f"timezone {config['timezone']!r} is not an IANA zone such as America/Toronto") from None

    return Location(float(config["latitude"]), float(config["longitude"]), config["timezone"])

def solar_position(julian_day):
    """(declination in radians, equation of time in minutes)."""
    t = (julian_day - J2000_JULIAN_DAY) / DAYS_PER_CENTURY
    mean_longitude = math.radians((280.46646 + t * (36000.76983 + t * 0.0003032)) % 360)
    mean_anomaly = math.radians(357.52911 + t * (35999.05029 - 0.0001537 * t))
    eccentricity = 0.016708634 - t * (0.000042037 + 0.0000001267 * t)
    centre = (math.sin(mean_anomaly) * (1.914602 - t * (0.004817 + 0.000014 * t))
              + math.sin(2 * mean_anomaly) * (0.019993 - 0.000101 * t)
              + math.sin(3 * mean_anomaly) * 0.000289)
    node = math.radians(125.04 - 1934.136 * t)
    apparent_longitude = math.radians(math.degrees(mean_longitude) + centre - 0.00569 - 0.00478 * math.sin(node))
    mean_obliquity = 23 + (26 + (21.448 - t * (46.815 + t * (0.00059 - t * 0.001813))) / 60) / 60
    obliquity = math.radians(mean_obliquity + 0.00256 * math.cos(node))

    declination = math.asin(math.sin(obliquity) * math.sin(apparent_longitude))
    y = math.tan(obliquity / 2) ** 2
    equation = (y * math.sin(2 * mean_longitude)
                - 2 * eccentricity * math.sin(mean_anomaly)
                + 4 * eccentricity * y * math.sin(mean_anomaly) * math.cos(2 * mean_longitude)
                - 0.5 * y * y * math.sin(4 * mean_longitude)
                - 1.25 * eccentricity * eccentricity * math.sin(2 * mean_anomaly))
    return declination, 4 * math.degrees(equation)

def sun_event_unix(day, latitude, longitude, rising):
    """Unix time of sunrise (rising) or sunset on the given date, None if the sun does not cross the horizon."""
    midnight = datetime(day.year, day.month, day.day, tzinfo=timezone.utc).timestamp()
    sign = -1 if rising else 1
    latitude_rad = math.radians(latitude)
    minutes = 720 - 4 * longitude

    for _ in range(3):
        declination, equation = solar_position(UNIX_EPOCH_JULIAN_DAY + (midnight + minutes * 60) / SECONDS_PER_DAY)
        cosine = (math.cos(math.radians(SUN_ZENITH_DEG)) / (math.cos(latitude_rad) * math.cos(declination))
                  - math.tan(latitude_rad) * math.tan(declination))
        if abs(cosine) > 1:
            return None
        hour_angle = math.degrees(math.acos(cosine))
        minutes = 720 - 4 * longitude - equation + sign * 4 * hour_angle

    return midnight + minutes * 60

def night_context(session_id, bedtime_unix, location):
    zone = ZoneInfo(location.timezone)
    bedtime = datetime.fromtimestamp(bedtime_unix, zone)
    night_of = datetime.fromtimestamp(bedtime_unix - NIGHT_SHIFT_S, zone).date()
    weekday = night_of.isoweekday()
    morning = night_of + timedelta(days=1)
    return {
        "session_id": session_id,
        "night_of": night_of,
        "weekday": weekday,
        "free_day_after": weekday in FREE_DAY_EVES,
        "utc_offset_min": int(bedtime.utcoffset().total_seconds() // 60),
        "sunset_unix": sun_event_unix(night_of, location.latitude, location.longitude, rising=False),
        "sunrise_unix": sun_event_unix(morning, location.latitude, location.longitude, rising=True),
    }

def nights_in_bed(connection):
    """(session_id, bedtime, up) for every session with a night."""
    return connection.execute(
        "SELECT sessions.session_id, sessions.started_unix, sessions.ended_unix FROM sessions "
        "WHERE sessions.session_id IN (SELECT session_id FROM nights) ORDER BY sessions.session_id").fetchall()

def store_context(connection, rows):
    values = []
    for row in rows:
        record = []
        for name in CONTEXT_COLUMNS:
            record.append(row[name])
        values.append(record)

    connection.begin()
    try:
        connection.execute("DELETE FROM night_context")
        if values:
            placeholders = ", ".join(["?"] * len(CONTEXT_COLUMNS))
            connection.executemany(f"INSERT INTO night_context ({', '.join(CONTEXT_COLUMNS)}) VALUES ({placeholders})",
                                   values)
    except Exception:
        connection.rollback()
        raise

    connection.commit()

def derive_context(connection, location):
    rows = []
    for session_id, bedtime_unix, _ in nights_in_bed(connection):
        rows.append(night_context(session_id, bedtime_unix, location))
    store_context(connection, rows)
    return rows

def night_hours(bedtime_unix, up_unix):
    first = bedtime_unix // SECONDS_PER_HOUR * SECONDS_PER_HOUR
    last = -(-up_unix // SECONDS_PER_HOUR) * SECONDS_PER_HOUR
    return list(range(first, last + 1, SECONDS_PER_HOUR))

def missing_dates(connection, now_unix):
    """UTC dates holding a past night hour not yet in weather, in order."""
    stored = set()
    for (hour_unix,) in connection.execute("SELECT hour_unix FROM weather").fetchall():
        stored.add(hour_unix)

    dates = set()
    for _, bedtime_unix, up_unix in nights_in_bed(connection):
        for hour_unix in night_hours(bedtime_unix, up_unix):
            if hour_unix <= now_unix and hour_unix not in stored:
                dates.add(datetime.fromtimestamp(hour_unix, timezone.utc).date())
    return sorted(dates)

def date_runs(dates):
    """Consecutive dates grouped into (first, last) pairs."""
    runs = []
    for day in dates:
        if runs and day - runs[-1][1] == timedelta(days=1):
            runs[-1][1] = day
        else:
            runs.append([day, day])

    pairs = []
    for first, last in runs:
        pairs.append((first, last))
    return pairs

def weather_url(location, first, last):
    query = urllib.parse.urlencode({
        "latitude": f"{location.latitude:.{LOCATION_DIGITS}f}",
        "longitude": f"{location.longitude:.{LOCATION_DIGITS}f}",
        "start_date": first.isoformat(),
        "end_date": last.isoformat(),
        "hourly": ",".join(WEATHER_VARIABLES),
        "timeformat": "unixtime",
        "timezone": "GMT",
    })
    return f"{WEATHER_URL}?{query}"

def fetch_json(url):
    request = urllib.request.Request(url, headers={"User-Agent": "nocturne"})
    try:
        with urllib.request.urlopen(request, timeout=WEATHER_TIMEOUT_S) as response:
            return json.load(response)
    except urllib.error.HTTPError as error:
        raise RuntimeError(f"Open-Meteo refused the request: HTTP {error.code} "
                           f"{error.read().decode('utf-8', errors='replace').strip()}") from None
    except urllib.error.URLError as error:
        raise RuntimeError(f"could not reach Open-Meteo: {error.reason}") from None

def parse_weather(body, now_unix):
    """(hour_unix, temperature, humidity, pressure, precipitation) for each past hour with any value."""
    hourly = body.get("hourly")
    if not isinstance(hourly, dict) or "time" not in hourly:
        raise RuntimeError("Open-Meteo answered without hourly data")

    hours = hourly["time"]
    columns = []
    for variable in WEATHER_VARIABLES:
        values = hourly.get(variable)
        if values is None or len(values) != len(hours):
            raise RuntimeError(f"Open-Meteo answered without a full {variable}")
        columns.append(values)

    rows = []
    for index, hour_unix in enumerate(hours):
        if hour_unix > now_unix:
            continue
        values = []
        for column in columns:
            values.append(column[index])
        if all(value is None for value in values):
            continue
        rows.append((int(hour_unix), *values))
    return rows

def store_weather(connection, rows, fetched_unix):
    if not rows:
        return
    values = []
    for row in rows:
        values.append((*row, fetched_unix))
    connection.executemany("INSERT OR REPLACE INTO weather VALUES (?, ?, ?, ?, ?, ?)", values)

def update_weather(connection, location, now_unix, fetch=fetch_json):
    """Fetch the missing night hours; returns how many hours were stored."""
    stored = 0
    for first, last in date_runs(missing_dates(connection, now_unix)):
        rows = parse_weather(fetch(weather_url(location, first, last)), now_unix)
        store_weather(connection, rows, now_unix)
        stored += len(rows)
    return stored

def clock_text(unix, zone):
    if unix is None:
        return "none"
    return datetime.fromtimestamp(unix, zone).strftime("%H:%M")

def number_text(value, unit, digits=0):
    if value is None:
        return "?"
    return f"{value:.{digits}f}{unit}"

def summary(connection, row, location):
    zone = ZoneInfo(location.timezone)
    weather = connection.execute(
        "SELECT temperature_c, humidity_pct, pressure_hpa, pressure_change_hpa, precipitation_mm, weather_hours "
        "FROM night_weather WHERE session_id = ?", [row["session_id"]]).fetchall()
    text = (f"night of {row['night_of']:%a %Y-%m-%d}{' (free day after)' if row['free_day_after'] else ''}, "
            f"sunset {clock_text(row['sunset_unix'], zone)}, sunrise {clock_text(row['sunrise_unix'], zone)}")
    if not weather or not weather[0][5]:
        return text + ", no weather"

    temperature, humidity, pressure, change, precipitation, _ = weather[0]
    return (text + f", {number_text(temperature, ' °C', 1)}, {number_text(humidity, ' %')}, "
            f"{number_text(pressure, ' hPa')} ({number_text(change, ' hPa', 1)}), "
            f"rain {number_text(precipitation, ' mm', 1)}")

def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--no-weather", action="store_true", help="skip fetching weather")
    parser.add_argument("--location", type=Path, help=f"location file (default {LOCATION_NAME} at the data root)")
    args = parser.parse_args()

    location = load_location(args.location)
    connection = connect()
    rows = derive_context(connection, location)
    if not args.no_weather:
        hours = update_weather(connection, location, int(time.time()))
        print(f"weather: {hours} hours stored")

    for row in rows:
        print(f"{row['session_id']}: {summary(connection, row, location)}")

if __name__ == "__main__":
    main()
