"""What each dashboard page reads, from the database.

Queries only: every metric is computed by the pipeline steps, and these
functions select and shape what a page shows. Times stay Unix seconds and
levels dB SPL; the front end formats them.
"""

import io
import math
import wave
from datetime import date

import numpy as np

from nocturne_pc.events import clock_unix, segment_clock
from nocturne_pc.nights import full_scale_db_spl

DEFAULT_SAMPLE_SHIFT = 9
ENVELOPE_BIN_S = 15
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

def trend_nights(connection, source, first_night, last_night):
    """Every column of trend_nights for the nights from first to last, oldest first."""
    return fetch_dicts(connection.execute(
        "SELECT * EXCLUDE (source) FROM trend_nights WHERE source = ? AND night_of BETWEEN ? AND ? "
        "ORDER BY night_of",
        [source, first_night, last_night]))

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
