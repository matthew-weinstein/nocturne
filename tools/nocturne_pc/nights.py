"""Night metrics, one row per session and source (design doc 9.13, §22).

    cd tools
    python -m nocturne_pc.nights
    python -m nocturne_pc.nights --force
    python -m nocturne_pc.nights --rules my_rules.toml

Each session and source with events is derived again whenever its events or
the rules (nights.toml) have changed since; --force derives every one.
Sessions shorter than min_session_min get no row.

Bedtime and up are the button presses. Everything else is inferred from sound:
onset is the start of the first quiet stretch (brief blips ignored), wake the first alarm or else
the start of the activity that runs into up. Events with no clock time (after
a missing segment) are left out. Loud moments are listed per window in
loud_moments. A session is replaced whole in one transaction.
"""

import argparse
import bisect
import hashlib
import json
import math
import time
import tomllib
from collections import namedtuple
from datetime import datetime
from pathlib import Path

import numpy as np

from nocturne_pc.db import connect
from nocturne_pc.events import clock_unix, segment_clock
from nocturne_pc.teacher import FRAME_MS, WINDOW_FRAMES
from nocturne_pc.yamnet import CLASS_GROUPS

RULES_PATH = Path(__file__).with_name("nights.toml")
NUMBER_KEYS = ("min_session_min", "onset_quiet_min", "onset_min_event_s", "wake_gap_min", "restless_join_s", "restless_min_s",
               "alarm_gap_s", "bathroom_window_min", "pause_min_s", "pause_max_s", "snort_score", "loud_peak_db")
CLASS_KEYS = ("onset_activity", "wake_activity")
NIGHT_COLUMNS = (
    "session_id", "source", "bedtime_unix", "up_unix", "time_in_bed_min",
    "onset_unix", "onset_latency_min", "wake_unix", "lie_in_min", "sleep_min", "efficiency", "midpoint_unix",
    "restless_bouts", "restless_min",
    "snoring_min", "snoring_share", "snoring_per_hour", "snoring_peak_db", "snoring_worst_hour_unix",
    "breathing_pauses", "breathing_pauses_per_hour",
    "coughs", "sneezes", "sleep_talk", "bathroom_trips",
    "alarms", "first_alarm_unix", "last_alarm_unix", "snooze_span_min", "alarm_to_up_min",
    "noise_floor_db", "loud_moments",
    "outage_min", "muted_min", "missing_min",
    "rules_sha256", "events_derived_unix", "derived_unix",
)
SNORT_CLASS = "Snort"
WINDOW_S = WINDOW_FRAMES * FRAME_MS / 1000
FRAME_S = FRAME_MS / 1000
SECONDS_PER_HOUR = 3600
NOISE_FLOOR_PERCENTILE = 10
MIC_REFERENCE_DB_SPL = 94
MIC_SENSITIVITY_DBFS = -26
PCM_BITS = 16
DEFAULT_SAMPLE_SHIFT = 9
DB_PER_BIT = 20 * math.log10(2)

Settings = namedtuple("Settings", NUMBER_KEYS + CLASS_KEYS)
Span = namedtuple("Span", ["start", "end"])

def load_settings(path=RULES_PATH):
    with open(path, "rb") as file:
        return parse_settings(tomllib.load(file))

def parse_settings(config):
    for key in config:
        if key not in NUMBER_KEYS + CLASS_KEYS:
            raise ValueError(f"unknown setting {key}; expected one of {', '.join(NUMBER_KEYS + CLASS_KEYS)}")

    for key in NUMBER_KEYS:
        if key not in config:
            raise ValueError(f"no {key}")
        value = config[key]
        if isinstance(value, bool) or not isinstance(value, (int, float)):
            raise ValueError(f"{key} must be a number, got {value!r}")
        if value < 0:
            raise ValueError(f"{key} cannot be negative")

    for key in CLASS_KEYS:
        if key not in config:
            raise ValueError(f"no {key}")
        if not isinstance(config[key], list):
            raise ValueError(f"{key} must be a list of classes")
        for name in config[key]:
            if name not in CLASS_GROUPS:
                raise ValueError(f"{key}: {name!r} is not a class; expected one of {', '.join(CLASS_GROUPS)}")

    if config["pause_min_s"] > config["pause_max_s"]:
        raise ValueError("pause_min_s cannot exceed pause_max_s")
    return Settings(**config)

def settings_digest(settings):
    text = json.dumps(settings._asdict(), sort_keys=True)
    return hashlib.sha256(text.encode("utf-8")).hexdigest()

def full_scale_db_spl(sample_shift):
    """The sound level at digital full scale for a sample shift (§2): about 78 dB SPL at 9."""
    return MIC_REFERENCE_DB_SPL - MIC_SENSITIVITY_DBFS - (PCM_BITS - sample_shift) * DB_PER_BIT

def join_spans(spans, gap_s):
    joined = []
    for span in sorted(spans):
        if joined and span.start - joined[-1].end <= gap_s:
            joined[-1] = Span(joined[-1].start, max(joined[-1].end, span.end))
        else:
            joined.append(span)
    return joined

def overlap_s(spans, start, end):
    total = 0.0
    for span in spans:
        total += max(0.0, min(span.end, end) - max(span.start, start))
    return total

def sleep_onset(bedtime, up, activity, quiet_s):
    """Start of the first quiet_s with no activity, from bedtime or the end of an activity; None if none fits."""
    candidates = [bedtime]
    for span in activity:
        if span.end > bedtime:
            candidates.append(span.end)
    candidates.sort()

    for start in candidates:
        if start + quiet_s > up:
            return None
        if overlap_s(activity, start, start + quiet_s) == 0:
            return start
    return None

def wake_time(after, up, activity, alarms, gap_s):
    """The first alarm after after; else the start of the activity that runs, gaps under gap_s, into up; else up."""
    for alarm in alarms:
        if alarm.start >= after:
            return alarm.start

    later = []
    for span in activity:
        if span.end > after:
            later.append(span)

    bouts = join_spans(later, gap_s)
    if bouts and up - bouts[-1].end <= gap_s:
        return max(bouts[-1].start, after)
    return up

def restless_bouts(movement, start, end, join_s, min_s):
    """Movement joined across gaps of join_s, at least min_s long, clipped to start..end."""
    bouts = []
    for bout in join_spans(movement, join_s):
        if bout.end - bout.start < min_s:
            continue
        clipped = Span(max(bout.start, start), min(bout.end, end))
        if clipped.end > clipped.start:
            bouts.append(clipped)
    return bouts

def breathing_pauses(snoring, others, followers, start, end, min_s, max_s):
    """Snoring events whose end, within start..end, is followed by min_s to max_s of nothing, then a gasp or snort.

    others are every sound that breaks the pause (snort windows included), sorted;
    followers are the start times of gasps and snorts.
    """
    starts = []
    for span in others:
        starts.append(span.start)

    count = 0
    for snore in snoring:
        if not start <= snore.end <= end:
            continue

        covered = False
        for span in others:
            if span.start <= snore.end < span.end:
                covered = True
                break
        if covered:
            continue

        index = bisect.bisect_right(starts, snore.end)
        if index == len(others):
            continue
        next_start = others[index].start
        if not min_s <= next_start - snore.end <= max_s:
            continue

        for follower in followers:
            if abs(follower - next_start) <= WINDOW_S:
                count += 1
                break
    return count

def bathroom_trips(triggers, water, start, end, window_s):
    """Footsteps or a door within start..end followed by water within window_s; each water ends a trip."""
    count = 0
    free_from = start
    for trigger in sorted(triggers):
        if trigger.start < free_from or trigger.start > end:
            continue
        for flow in sorted(water):
            if trigger.start <= flow.start <= trigger.start + window_s:
                count += 1
                free_from = flow.end
                break
    return count

def worst_hour(spans):
    """Unix start of the clock hour holding the most of spans, the earliest on a tie; None if no spans."""
    by_hour = {}
    for span in spans:
        hour = int(span.start // SECONDS_PER_HOUR)
        while hour * SECONDS_PER_HOUR < span.end:
            seconds = min(span.end, (hour + 1) * SECONDS_PER_HOUR) - max(span.start, hour * SECONDS_PER_HOUR)
            by_hour[hour] = by_hour.get(hour, 0.0) + seconds
            hour += 1

    if not by_hour:
        return None
    return max(sorted(by_hour), key=by_hour.get) * SECONDS_PER_HOUR

def loudest_in(times, peaks, spans):
    """Highest peak of a window overlapping any span; times are window starts, sorted."""
    loudest = None
    for span in spans:
        index = bisect.bisect_left(times, span.start - WINDOW_S)
        while index < len(times) and times[index] < span.end:
            if times[index] + WINDOW_S > span.start and (loudest is None or peaks[index] > loudest):
                loudest = peaks[index]
            index += 1
    return loudest

def minutes(seconds):
    if seconds is None:
        return None
    return seconds / 60

def spans_of(events, classes):
    spans = []
    for class_name, span in events:
        if class_name in classes:
            spans.append(span)
    spans.sort()
    return spans

def night_metrics(night, settings):
    """The metrics of one night from its gathered inputs (see gather_night)."""
    bedtime = night["bedtime"]
    up = night["up"]
    events = night["events"]
    outages = night["outages"]
    offset_db = full_scale_db_spl(night["sample_shift"])

    outage_s = overlap_s(outages, bedtime, up)
    time_in_bed_s = up - bedtime - outage_s

    alarms = join_spans(spans_of(events, {"alarm"}), settings.alarm_gap_s)
    onset_activity = []
    for span in spans_of(events, set(settings.onset_activity)):
        if span.end - span.start >= settings.onset_min_event_s:
            onset_activity.append(span)
    onset = sleep_onset(bedtime, up, onset_activity, settings.onset_quiet_min * 60)
    after = bedtime if onset is None else onset
    wake = wake_time(after, up, spans_of(events, set(settings.wake_activity)), alarms, settings.wake_gap_min * 60)

    metrics = {
        "bedtime_unix": bedtime,
        "up_unix": up,
        "time_in_bed_min": minutes(time_in_bed_s),
        "onset_unix": onset,
        "onset_latency_min": None,
        "wake_unix": wake,
        "lie_in_min": minutes(up - wake),
        "sleep_min": None,
        "efficiency": None,
        "midpoint_unix": None,
        "restless_bouts": None,
        "restless_min": None,
        "breathing_pauses": None,
        "breathing_pauses_per_hour": None,
        "sleep_talk": None,
        "bathroom_trips": None,
        "snoring_share": None,
    }

    snoring = spans_of(events, {"snoring"})
    snoring_s = overlap_s(snoring, bedtime, up)
    metrics["snoring_min"] = minutes(snoring_s)
    metrics["snoring_per_hour"] = minutes(snoring_s) / (time_in_bed_s / SECONDS_PER_HOUR)
    metrics["snoring_worst_hour_unix"] = worst_hour(snoring)

    times = night["level_times"]
    loudest = loudest_in(times, night["level_peaks"], snoring)
    metrics["snoring_peak_db"] = None if loudest is None else loudest + offset_db

    metrics["coughs"] = len(spans_of(events, {"cough"}))
    metrics["sneezes"] = len(spans_of(events, {"sneeze"}))

    metrics["alarms"] = len(alarms)
    metrics["first_alarm_unix"] = alarms[0].start if alarms else None
    metrics["last_alarm_unix"] = alarms[-1].start if alarms else None
    metrics["snooze_span_min"] = minutes(alarms[-1].start - alarms[0].start) if alarms else None
    metrics["alarm_to_up_min"] = minutes(up - alarms[-1].start) if alarms else None

    rms = night["level_rms"]
    metrics["noise_floor_db"] = float(np.percentile(rms, NOISE_FLOOR_PERCENTILE)) + offset_db if rms else None
    metrics["loud_moments"] = len(night["loud_moments"])

    metrics["outage_min"] = minutes(outage_s)
    metrics["muted_min"] = minutes(night["muted_s"])
    metrics["missing_min"] = minutes(max(0.0, time_in_bed_s - night["audio_s"]))

    if onset is None:
        return metrics

    bouts = restless_bouts(spans_of(events, {"movement"}), onset, wake,
                           settings.restless_join_s, settings.restless_min_s)
    restless_s = overlap_s(bouts, onset, wake)
    sleep_s = max(0.0, wake - onset - restless_s - overlap_s(outages, onset, wake))

    metrics["onset_latency_min"] = minutes(onset - bedtime)
    metrics["sleep_min"] = minutes(sleep_s)
    metrics["efficiency"] = sleep_s / time_in_bed_s
    metrics["midpoint_unix"] = onset + (wake - onset) / 2
    metrics["restless_bouts"] = len(bouts)
    metrics["restless_min"] = minutes(restless_s)
    if sleep_s > 0:
        metrics["snoring_share"] = overlap_s(snoring, onset, wake) / sleep_s

    snorts = night["snorts"]
    others = []
    for class_name, span in events:
        if class_name != "silence":
            others.append(span)
    for snort in snorts:
        others.append(Span(snort, snort + WINDOW_S))
    others.sort()
    followers = list(snorts)
    for span in spans_of(events, {"gasp"}):
        followers.append(span.start)

    pauses = breathing_pauses(snoring, others, followers, onset, wake, settings.pause_min_s, settings.pause_max_s)
    metrics["breathing_pauses"] = pauses
    if sleep_s > 0:
        metrics["breathing_pauses_per_hour"] = pauses / (sleep_s / SECONDS_PER_HOUR)

    speech = spans_of(events, {"speech"})
    talk = 0
    for span in speech:
        if onset <= span.start < wake:
            talk += 1
    metrics["sleep_talk"] = talk

    metrics["bathroom_trips"] = bathroom_trips(spans_of(events, {"footsteps", "door"}), spans_of(events, {"water"}),
                                               onset, wake, settings.bathroom_window_min * 60)
    return metrics

def gather_night(connection, session_id, source, settings):
    """Everything night_metrics reads, from the database."""
    started, ended, sample_shift, muted_ms = connection.execute(
        "SELECT started_unix, ended_unix, sample_shift, muted_ms FROM sessions WHERE session_id = ?",
        [session_id]).fetchone()
    if sample_shift is None:
        sample_shift = DEFAULT_SAMPLE_SHIFT

    segment_rows = connection.execute(
        "SELECT segment, frames, corrupt FROM segments WHERE session_id = ?", [session_id]).fetchall()
    runs = connection.execute(
        "SELECT run, first_segment, anchor_unix FROM runs WHERE session_id = ?", [session_id]).fetchall()
    clock = segment_clock(segment_rows, runs)

    audio_frames = 0
    for _, frames, _ in segment_rows:
        audio_frames += frames

    events = []
    for class_name, start, end in connection.execute(
            "SELECT class, start_unix, end_unix FROM events WHERE session_id = ? AND source = ? "
            "AND start_unix IS NOT NULL AND end_unix IS NOT NULL ORDER BY start_unix",
            [session_id, source]).fetchall():
        events.append((class_name, Span(start, end)))

    outages = []
    for start, end in connection.execute(
            "SELECT start_unix, end_unix FROM outages WHERE session_id = ? AND start_unix IS NOT NULL",
            [session_id]).fetchall():
        outages.append(Span(start, end))

    levels = []
    for segment, first_frame, rms_dbfs, peak_dbfs in connection.execute(
            "SELECT segment, first_frame, rms_dbfs, peak_dbfs FROM levels WHERE session_id = ?",
            [session_id]).fetchall():
        moment = clock_unix(clock, segment, first_frame)
        if moment is not None:
            levels.append((moment, rms_dbfs, peak_dbfs))
    levels.sort()

    snorts = []
    for segment, first_frame in connection.execute(
            "SELECT segment, first_frame FROM teacher WHERE session_id = ? AND yamnet_class = ? AND score >= ?",
            [session_id, SNORT_CLASS, settings.snort_score]).fetchall():
        moment = clock_unix(clock, segment, first_frame)
        if moment is not None:
            snorts.append(moment)
    snorts.sort()

    offset_db = full_scale_db_spl(sample_shift)
    loud = []
    for segment, first_frame, peak_dbfs, rms_dbfs, top_class in connection.execute(
            "SELECT levels.segment, levels.first_frame, levels.peak_dbfs, levels.rms_dbfs, teacher.yamnet_class "
            "FROM levels LEFT JOIN teacher ON teacher.session_id = levels.session_id "
            "AND teacher.segment = levels.segment AND teacher.first_frame = levels.first_frame AND teacher.rank = 1 "
            "WHERE levels.session_id = ? AND levels.peak_dbfs >= ? ORDER BY levels.segment, levels.first_frame",
            [session_id, settings.loud_peak_db - offset_db]).fetchall():
        loud.append((session_id, segment, first_frame, clock_unix(clock, segment, first_frame),
                     peak_dbfs + offset_db, rms_dbfs + offset_db, top_class))

    level_times = []
    level_rms = []
    level_peaks = []
    for moment, rms_dbfs, peak_dbfs in levels:
        level_times.append(moment)
        level_rms.append(rms_dbfs)
        level_peaks.append(peak_dbfs)

    return {
        "bedtime": float(started),
        "up": float(ended),
        "sample_shift": sample_shift,
        "muted_s": (muted_ms or 0) / 1000,
        "audio_s": audio_frames * FRAME_S,
        "events": events,
        "outages": outages,
        "level_times": level_times,
        "level_rms": level_rms,
        "level_peaks": level_peaks,
        "snorts": snorts,
        "loud_moments": loud,
    }

def store_night(connection, session_id, source, metrics, loud_moments):
    row = dict(metrics, session_id=session_id, source=source)
    values = []
    for name in NIGHT_COLUMNS:
        values.append(row[name])
    placeholders = ", ".join(["?"] * len(NIGHT_COLUMNS))

    connection.begin()
    try:
        connection.execute("DELETE FROM nights WHERE session_id = ? AND source = ?", [session_id, source])
        connection.execute("DELETE FROM loud_moments WHERE session_id = ?", [session_id])
        connection.execute(f"INSERT INTO nights ({', '.join(NIGHT_COLUMNS)}) VALUES ({placeholders})", values)
        if loud_moments:
            connection.executemany("INSERT INTO loud_moments VALUES (?, ?, ?, ?, ?, ?, ?)", loud_moments)
    except Exception:
        connection.rollback()
        raise

    connection.commit()

def derive_night(connection, session_id, source, settings, digest, events_derived_unix, derived_unix):
    night = gather_night(connection, session_id, source, settings)
    metrics = night_metrics(night, settings)
    metrics["rules_sha256"] = digest
    metrics["events_derived_unix"] = events_derived_unix
    metrics["derived_unix"] = derived_unix
    store_night(connection, session_id, source, metrics, night["loud_moments"])
    return metrics

def nights_to_derive(connection, settings, digest, force):
    """(session_id, source, events derived_unix) long enough, whose night is missing or older than its events or rules."""
    return connection.execute(
        "SELECT evented.session_id, evented.source, evented.derived_unix FROM evented "
        "JOIN sessions ON sessions.session_id = evented.session_id "
        "LEFT JOIN nights ON nights.session_id = evented.session_id AND nights.source = evented.source "
        "WHERE sessions.ended_unix - sessions.started_unix >= ? "
        "AND (? OR nights.session_id IS NULL OR nights.rules_sha256 <> ? "
        "OR nights.events_derived_unix <> evented.derived_unix) "
        "ORDER BY evented.session_id, evented.source",
        [settings.min_session_min * 60, force, digest]).fetchall()

def clock_text(unix):
    if unix is None:
        return "?"
    return datetime.fromtimestamp(unix).strftime("%H:%M")

def number_text(value, unit, scale=1.0, digits=0):
    if value is None:
        return "?"
    return f"{value * scale:.{digits}f}{unit}"

def summary(metrics):
    return (f"in bed {number_text(metrics['time_in_bed_min'], ' h', 1 / 60, 1)}, "
            f"onset {clock_text(metrics['onset_unix'])} after {number_text(metrics['onset_latency_min'], ' min')}, "
            f"wake {clock_text(metrics['wake_unix'])}, "
            f"sleep {number_text(metrics['sleep_min'], ' h', 1 / 60, 1)} "
            f"({number_text(metrics['efficiency'], ' %', 100)}), "
            f"restless {metrics['restless_bouts'] if metrics['restless_bouts'] is not None else '?'}, "
            f"snoring {number_text(metrics['snoring_min'], ' min', 1, 1)}, "
            f"coughs {metrics['coughs']}, alarms {metrics['alarms']}, "
            f"floor {number_text(metrics['noise_floor_db'], ' dB')}")

def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--force", action="store_true", help="derive nights that are current")
    parser.add_argument("--rules", type=Path, default=RULES_PATH, help=f"rules file (default {RULES_PATH.name})")
    args = parser.parse_args()

    settings = load_settings(args.rules)
    digest = settings_digest(settings)
    connection = connect()
    pending = nights_to_derive(connection, settings, digest, args.force)
    if not pending:
        print("nights are current")
        return

    for session_id, source, events_derived_unix in pending:
        metrics = derive_night(connection, session_id, source, settings, digest, events_derived_unix,
                               int(time.time()))
        print(f"night {session_id} {source}: {summary(metrics)}")

if __name__ == "__main__":
    main()
