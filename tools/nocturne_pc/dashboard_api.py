"""What each dashboard page reads, from the database.

Queries only: every metric is computed by the pipeline steps, and these
functions select and shape what a page shows. Times stay Unix seconds and
levels dB SPL; the front end formats them.
"""

import io
import math
import wave
from datetime import date, timedelta

import numpy as np

from nocturne_pc.events import clock_unix, segment_clock
from nocturne_pc.nights import full_scale_db_spl

DEFAULT_SAMPLE_SHIFT = 9
ENVELOPE_BIN_S = 15
MONTH_BIN_S = 120
MONTH_DAYS = 30
MAX_CLIP_FRAMES = 30000

SESSION_FIELDS = (
    "session_id", "started_unix", "ended_unix", "segment_count", "sample_shift", "firmware_version",
    "time_reliable", "resumed", "resume_count", "recovered", "muted_ms", "buffer_overflows",
    "sd_write_errors", "upload_retries", "wifi_disconnects",
)

EVENT_FIELDS = (
    "class", "start_segment", "start_frame", "end_segment", "end_frame", "start_unix", "end_unix",
    "duration_ms", "peak_score",
)

def json_value(value):
    if isinstance(value, date):
        return value.isoformat()
    if isinstance(value, float) and not math.isfinite(value):
        return None
    return value

def fetch_dicts(cursor):
    names = []
    for column in cursor.description:
        names.append(column[0])
    rows = []
    for values in cursor.fetchall():
        row = {}
        for name, value in zip(names, values):
            row[name] = json_value(value)
        rows.append(row)
    return rows

def fetch_dict(cursor):
    rows = fetch_dicts(cursor)
    if not rows:
        return None
    return rows[0]

def list_nights(connection, source):
    """Every night with metrics, newest first: one row per date, its longest session."""
    return fetch_dicts(connection.execute(
        "SELECT trend_nights.session_id, trend_nights.night_of, trend_nights.weekday, trend_nights.free_day_after, "
        "trend_nights.timezone, trend_nights.sessions, "
        "nights.bedtime_unix, nights.onset_unix, nights.wake_unix, nights.up_unix, "
        "nights.sleep_min, nights.efficiency, nights.onset_latency_min, "
        "night_context.sunrise_unix, night_context.sunset_unix "
        "FROM trend_nights "
        "JOIN nights ON nights.session_id = trend_nights.session_id AND nights.source = trend_nights.source "
        "JOIN night_context ON night_context.session_id = trend_nights.session_id "
        "WHERE trend_nights.source = ? ORDER BY trend_nights.night_of DESC",
        [source]))

def level_envelope(levels, start_unix, end_unix, bin_s, offset_db):
    """The loudest peak in each bin from start to end, in dB SPL; None where no window fell.

    levels are (moment_unix, peak_dbfs).
    """
    num_bins = max(1, math.ceil((end_unix - start_unix) / bin_s))
    peaks = [None] * num_bins
    for moment, peak_dbfs in levels:
        if moment < start_unix or moment >= end_unix:
            continue
        index = int((moment - start_unix) // bin_s)
        level = round(peak_dbfs + offset_db, 1)
        if peaks[index] is None or level > peaks[index]:
            peaks[index] = level
    return peaks

def session_clock(connection, session_id):
    segment_rows = connection.execute(
        "SELECT segment, frames, corrupt FROM segments WHERE session_id = ?", [session_id]).fetchall()
    runs = connection.execute(
        "SELECT run, first_segment, anchor_unix FROM runs WHERE session_id = ?", [session_id]).fetchall()
    return segment_clock(segment_rows, runs)

def night_levels(connection, session_id, start_unix, end_unix, sample_shift, bin_s):
    clock = session_clock(connection, session_id)
    levels = []
    for segment, first_frame, peak_dbfs in connection.execute(
            "SELECT segment, first_frame, peak_dbfs FROM levels WHERE session_id = ?", [session_id]).fetchall():
        moment = clock_unix(clock, segment, first_frame)
        if moment is not None:
            levels.append((moment, peak_dbfs))
    peaks = level_envelope(levels, start_unix, end_unix, bin_s, full_scale_db_spl(sample_shift))
    return {"start_unix": start_unix, "bin_s": bin_s, "peak_db": peaks}

def neighbours(nights, session_id):
    """The session IDs of the night before and after, in list_nights' newest-first order."""
    for index, night in enumerate(nights):
        if night["session_id"] != session_id:
            continue
        previous = None
        following = None
        if index + 1 < len(nights):
            previous = nights[index + 1]["session_id"]
        if index > 0:
            following = nights[index - 1]["session_id"]
        return previous, following
    return None, None

def night_detail(connection, session_id, source, bin_s=ENVELOPE_BIN_S):
    """Everything the Night page shows for one session, or None if it has no night."""
    session = fetch_dict(connection.execute(
        f"SELECT {', '.join(SESSION_FIELDS)} FROM sessions WHERE session_id = ?", [session_id]))
    metrics = fetch_dict(connection.execute(
        "SELECT * EXCLUDE (session_id, source, rules_sha256, events_derived_unix, derived_unix) "
        "FROM nights WHERE session_id = ? AND source = ?",
        [session_id, source]))
    if session is None or metrics is None:
        return None
    if session["sample_shift"] is None:
        session["sample_shift"] = DEFAULT_SAMPLE_SHIFT

    context = fetch_dict(connection.execute(
        "SELECT * EXCLUDE (session_id) FROM night_context WHERE session_id = ?", [session_id]))
    weather = fetch_dict(connection.execute(
        "SELECT * EXCLUDE (session_id) FROM night_weather WHERE session_id = ?", [session_id]))
    events = fetch_dicts(connection.execute(
        f"SELECT {', '.join(EVENT_FIELDS)} FROM events WHERE session_id = ? AND source = ? "
        "AND start_unix IS NOT NULL ORDER BY start_unix, class",
        [session_id, source]))
    outages = fetch_dicts(connection.execute(
        "SELECT start_unix, end_unix FROM outages WHERE session_id = ? AND start_unix IS NOT NULL "
        "ORDER BY start_unix",
        [session_id]))
    levels = night_levels(connection, session_id, metrics["bedtime_unix"], metrics["up_unix"],
                          session["sample_shift"], bin_s)

    previous, following = neighbours(list_nights(connection, source), session_id)
    return {
        "session": session,
        "metrics": metrics,
        "context": context,
        "weather": weather,
        "events": events,
        "outages": outages,
        "levels": levels,
        "previous": previous,
        "next": following,
    }

def sleep_target_min(connection):
    row = connection.execute("SELECT sleep_target_h FROM trend_settings").fetchone()
    if row is None:
        return None
    return row[0] * 60

def night_range(connection, source):
    return connection.execute(
        "SELECT MIN(night_of), MAX(night_of) FROM trend_nights WHERE source = ?", [source]).fetchone()

def trend_nights(connection, source, first_night, last_night):
    """Every column of trend_nights for the nights from first to last, oldest first, and the sleep target."""
    nights = fetch_dicts(connection.execute(
        "SELECT * EXCLUDE (source) FROM trend_nights WHERE source = ? AND night_of BETWEEN ? AND ? "
        "ORDER BY night_of",
        [source, first_night, last_night]))
    earliest, latest = night_range(connection, source)
    return {
        "sleep_target_min": sleep_target_min(connection),
        "earliest": json_value(earliest),
        "latest": json_value(latest),
        "nights": nights,
    }

def month(connection, source, last_night=None, days=MONTH_DAYS, bin_s=MONTH_BIN_S):
    """The days up to last_night (default the latest night): each night's times, metrics and level envelope."""
    earliest, latest = night_range(connection, source)
    if last_night is None:
        last_night = latest
    if last_night is None:
        last_night = date.today()
    first_night = last_night - timedelta(days=days - 1)

    nights = fetch_dicts(connection.execute(
        "SELECT trend_nights.session_id, trend_nights.night_of, trend_nights.weekday, "
        "trend_nights.free_day_after, trend_nights.timezone, "
        "trend_nights.bedtime_clock_min, trend_nights.onset_clock_min, trend_nights.wake_clock_min, "
        "trend_nights.up_clock_min, trend_nights.midpoint_clock_min, trend_nights.sunrise_clock_min, "
        "trend_nights.sleep_min, trend_nights.onset_latency_min, trend_nights.efficiency, "
        "nights.bedtime_unix, nights.up_unix, sessions.sample_shift "
        "FROM trend_nights "
        "JOIN nights ON nights.session_id = trend_nights.session_id AND nights.source = trend_nights.source "
        "JOIN sessions ON sessions.session_id = trend_nights.session_id "
        "WHERE trend_nights.source = ? AND trend_nights.night_of BETWEEN ? AND ? "
        "ORDER BY trend_nights.night_of DESC",
        [source, first_night, last_night]))
    for night in nights:
        sample_shift = night.pop("sample_shift")
        if sample_shift is None:
            sample_shift = DEFAULT_SAMPLE_SHIFT
        night["levels"] = night_levels(connection, night["session_id"], night["bedtime_unix"],
                                       night["up_unix"], sample_shift, bin_s)

    debt = connection.execute(
        "SELECT debt_14_min, nights_14 FROM trend_sleep_debt WHERE source = ? AND night_of <= ? "
        "AND night_of >= ? ORDER BY night_of DESC LIMIT 1",
        [source, last_night, last_night - timedelta(days=13)]).fetchone()
    debt_14_min = None
    if debt is not None:
        debt_14_min = debt[0]

    return {
        "first": first_night.isoformat(),
        "last": last_night.isoformat(),
        "earliest": json_value(earliest),
        "latest": json_value(latest),
        "sleep_target_min": sleep_target_min(connection),
        "debt_14_min": debt_14_min,
        "nights": nights,
    }

def status(connection):
    last_ingested, num_sessions = connection.execute(
        "SELECT MAX(ingested_unix), COUNT(*) FROM sessions").fetchone()
    return {"last_ingested_unix": last_ingested, "sessions": num_sessions}

def clip_frames(num_frames):
    """The frames a clip request may decode: at least 1, at most MAX_CLIP_FRAMES (10 minutes)."""
    return min(MAX_CLIP_FRAMES, max(1, num_frames))

def wav_bytes(samples, sample_rate):
    """Float samples in [-1, 1] as a mono 16-bit WAV file."""
    clipped = np.clip(samples, -1.0, 1.0)
    pcm = np.round(clipped * 32767).astype("<i2")
    buffer = io.BytesIO()
    with wave.open(buffer, "wb") as file:
        file.setnchannels(1)
        file.setsampwidth(2)
        file.setframerate(sample_rate)
        file.writeframes(pcm.tobytes())
    return buffer.getvalue()
