"""Trends over the nights, as DuckDB views.

    cd tools
    python -m nocturne_pc.trends
    python -m nocturne_pc.trends --settings my_trends.toml

The trend_* views are part of the schema and always current; this stores the
settings they read (trends.toml: the nightly sleep target, the illness flag
and how many nights a predictor needs) into trend_settings, and prints where
the latest night stands.
"""

import argparse
import tomllib
from collections import namedtuple
from pathlib import Path

from nocturne_pc.db import connect

SETTINGS_PATH = Path(__file__).with_name("trends.toml")
NUMBER_KEYS = ("sleep_target_h", "illness_factor")
COUNT_KEYS = ("illness_min_count", "illness_min_baseline_nights", "predictor_min_nights")
SETTING_KEYS = NUMBER_KEYS + COUNT_KEYS
HOURS_PER_DAY = 24
NOON_MIN = 12 * 60

Settings = namedtuple("Settings", SETTING_KEYS)

def load_settings(path=SETTINGS_PATH):
    with open(path, "rb") as file:
        return parse_settings(tomllib.load(file))

def parse_settings(config):
    for key in config:
        if key not in SETTING_KEYS:
            raise ValueError(f"unknown setting {key}; expected one of {', '.join(SETTING_KEYS)}")

    for key in SETTING_KEYS:
        if key not in config:
            raise ValueError(f"no {key}")
        value = config[key]
        if isinstance(value, bool) or not isinstance(value, (int, float)):
            raise ValueError(f"{key} must be a number, got {value!r}")
        if key in COUNT_KEYS and not isinstance(value, int):
            raise ValueError(f"{key} must be a whole number")
        if value < 0:
            raise ValueError(f"{key} cannot be negative")

    if config["sleep_target_h"] > HOURS_PER_DAY:
        raise ValueError(f"sleep_target_h cannot exceed {HOURS_PER_DAY}")
    return Settings(**config)

def store_settings(connection, settings):
    placeholders = ", ".join(["?"] * len(SETTING_KEYS))
    connection.begin()
    try:
        connection.execute("DELETE FROM trend_settings")
        connection.execute(f"INSERT INTO trend_settings ({', '.join(SETTING_KEYS)}) VALUES ({placeholders})",
                           list(settings))
    except Exception:
        connection.rollback()
        raise

    connection.commit()

def clock_text(clock_min):
    if clock_min is None:
        return "?"
    minutes = int(round(clock_min + NOON_MIN)) % (HOURS_PER_DAY * 60)
    return f"{minutes // 60:02d}:{minutes % 60:02d}"

def number_text(value, unit, scale=1.0, digits=0):
    if value is None:
        return "?"
    return f"{value * scale:.{digits}f}{unit}"

def rolling_mean(connection, source, night_of, metric):
    rows = connection.execute("SELECT mean_7 FROM trend_rolling WHERE source = ? AND night_of = ? AND metric = ?",
                              [source, night_of, metric]).fetchall()
    if not rows:
        return None
    return rows[0][0]

def summary(connection, source):
    """One line on the latest night of a source, or None if it has none."""
    latest = connection.execute("SELECT MAX(night_of) FROM trend_nights WHERE source = ?", [source]).fetchall()
    night_of = latest[0][0]
    if night_of is None:
        return None

    sri = connection.execute("SELECT sri_7 FROM trend_consistency WHERE source = ? AND night_of = ?",
                             [source, night_of]).fetchall()
    debt = connection.execute("SELECT debt_14_min FROM trend_sleep_debt WHERE source = ? AND night_of = ?",
                              [source, night_of]).fetchall()
    illness = connection.execute("SELECT cough_flag, sneeze_flag FROM trend_illness WHERE source = ? AND night_of = ?",
                                 [source, night_of]).fetchall()

    text = (f"{source} to {night_of:%a %Y-%m-%d}, last 7 nights: "
            f"sleep {number_text(rolling_mean(connection, source, night_of, 'sleep_min'), ' h', 1 / 60, 1)}, "
            f"efficiency {number_text(rolling_mean(connection, source, night_of, 'efficiency'), ' %', 100)}, "
            f"bedtime {clock_text(rolling_mean(connection, source, night_of, 'bedtime_clock_min'))}, "
            f"wake {clock_text(rolling_mean(connection, source, night_of, 'wake_clock_min'))}, "
            f"regularity {number_text(sri[0][0] if sri else None, '')}; "
            f"14-night debt {number_text(debt[0][0] if debt else None, ' h', 1 / 60, 1)}")

    flags = []
    if illness and illness[0][0]:
        flags.append("coughing above usual")
    if illness and illness[0][1]:
        flags.append("sneezing above usual")
    if flags:
        text += "; " + ", ".join(flags)
    return text

def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--settings", type=Path, default=SETTINGS_PATH,
                        help=f"settings file (default {SETTINGS_PATH.name})")
    args = parser.parse_args()

    settings = load_settings(args.settings)
    connection = connect()
    store_settings(connection, settings)

    sources = connection.execute("SELECT DISTINCT source FROM trend_nights ORDER BY source").fetchall()
    if not sources:
        print("no nights with context yet: run nights and context first")
        return

    for (source,) in sources:
        print(summary(connection, source))

if __name__ == "__main__":
    main()
