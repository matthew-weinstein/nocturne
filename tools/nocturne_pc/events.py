"""Events from window scores by per-class rules.

    cd tools
    python -m nocturne_pc.events
    python -m nocturne_pc.events --force
    python -m nocturne_pc.events --rules my_rules.toml

Each scored session and source is derived again whenever its scores or the
rules have changed since it was last derived; --force derives every one.

A class's events are runs of back-to-back windows scoring at least its off
threshold that reach its on threshold somewhere. Events closer than merge_ms
join, then any shorter than min_ms is dropped. Nothing joins across a stretch
break (a resume, or a missing or corrupt segment), and a class with no rule
gets no events. A session and source is replaced whole in one transaction.
"""

import argparse
import hashlib
import json
import math
import time
import tomllib
from collections import namedtuple
from pathlib import Path

import numpy as np

from nocturne_pc.db import connect
from nocturne_pc.teacher import FRAME_MS, WINDOW_FRAMES, stretches
from nocturne_pc.yamnet import CLASS_GROUPS, OTHER_GROUP

RULES_PATH = Path(__file__).with_name("events.toml")
RULE_KEYS = ("on", "off", "min_ms", "merge_ms")
SECTIONS = ("default", "classes")
EVENT_COLUMNS = ("session_id", "source", "class", "start_segment", "start_frame", "end_segment", "end_frame",
                 "start_unix", "end_unix", "duration_ms", "windows", "peak_score", "mean_score")
CLOCK_COLUMNS = ("start_unix", "end_unix")

Rule = namedtuple("Rule", RULE_KEYS)
Event = namedtuple("Event", ["start", "end", "windows", "peak", "total"])
Stretch = namedtuple("Stretch", ["segments", "offsets", "frames"])

def load_rules(path=RULES_PATH):
    with open(path, "rb") as file:
        return parse_rules(tomllib.load(file))

def parse_rules(config):
    for section in config:
        if section not in SECTIONS:
            raise ValueError(f"unknown section [{section}]; expected [default] or [classes.<name>]")

    known = list(CLASS_GROUPS) + [OTHER_GROUP]
    default = config.get("default", {})
    rules = {}
    for name, overrides in config.get("classes", {}).items():
        if name not in known:
            raise ValueError(f"[classes.{name}] is not a class; expected one of {', '.join(known)}")
        rules[name] = make_rule(name, default, overrides)
    return rules

def make_rule(name, default, overrides):
    values = {}
    for key in list(default) + list(overrides):
        if key not in RULE_KEYS:
            raise ValueError(f"{name}: unknown setting {key}; expected one of {', '.join(RULE_KEYS)}")

    for key in RULE_KEYS:
        if key in overrides:
            value = overrides[key]
        elif key in default:
            value = default[key]
        else:
            raise ValueError(f"{name}: no {key}, and none in [default]")
        if isinstance(value, bool) or not isinstance(value, (int, float)):
            raise ValueError(f"{name}: {key} must be a number, got {value!r}")
        values[key] = value

    rule = Rule(**values)
    if not 0.0 <= rule.off <= rule.on <= 1.0:
        raise ValueError(f"{name}: needs 0 <= off <= on <= 1, got off {rule.off} and on {rule.on}")
    if rule.min_ms < 0 or rule.merge_ms < 0:
        raise ValueError(f"{name}: min_ms and merge_ms cannot be negative")
    return rule

def rules_digest(rules):
    settings = {}
    for name, rule in rules.items():
        settings[name] = rule._asdict()
    text = json.dumps(settings, sort_keys=True)
    return hashlib.sha256(text.encode("utf-8")).hexdigest()

def candidate_events(windows, rule):
    """Runs of back-to-back windows at or above off, kept if one reaches on.

    windows are (start, end, score), in frames from the stretch start, in order.
    """
    candidates = []
    current = None

    for start, end, score in windows:
        if score < rule.off:
            if current is not None:
                candidates.append(current)
                current = None
            continue

        if current is not None and start == current.end:
            current = Event(current.start, end, current.windows + 1, max(current.peak, score), current.total + score)
        else:
            if current is not None:
                candidates.append(current)
            current = Event(start, end, 1, score, score)

    if current is not None:
        candidates.append(current)

    kept = []
    for candidate in candidates:
        if candidate.peak >= rule.on:
            kept.append(candidate)
    return kept

def merge_events(events, merge_ms):
    merged = []
    for event in events:
        if merged and (event.start - merged[-1].end) * FRAME_MS <= merge_ms:
            last = merged[-1]
            merged[-1] = Event(last.start, event.end, last.windows + event.windows,
                               max(last.peak, event.peak), last.total + event.total)
        else:
            merged.append(event)
    return merged

def stretch_events(windows, rule):
    kept = []
    for event in merge_events(candidate_events(windows, rule), rule.merge_ms):
        if (event.end - event.start) * FRAME_MS >= rule.min_ms:
            kept.append(event)
    return kept

def stretch_layouts(segment_rows, run_starts):
    """Each stretch's segments, their frame offsets from its start, and its length in frames."""
    frames_by_segment = {}
    for segment, frames, _ in segment_rows:
        frames_by_segment[segment] = frames

    layouts = []
    session_stretches, _ = stretches(segment_rows, run_starts, set(frames_by_segment))
    for segments in session_stretches:
        offsets = []
        total = 0
        for segment in segments:
            offsets.append(total)
            total += frames_by_segment[segment]
        layouts.append(Stretch(segments, offsets, total))
    return layouts

def locate(stretch, position):
    """(segment, frame) of a frame position within the stretch."""
    index = len(stretch.segments) - 1
    while stretch.offsets[index] > position:
        index -= 1
    return stretch.segments[index], position - stretch.offsets[index]

def segment_clock(segment_rows, runs):
    """Unix time of each segment's first frame: its run's anchor plus the run's frames before it.

    runs are (run, first_segment, anchor_unix). A segment after one with no
    row in its run has no time, since the missing one's length is unknown.
    """
    frames_by_segment = {}
    for segment, frames, _ in segment_rows:
        frames_by_segment[segment] = frames
    last_segment = max(frames_by_segment, default=-1)

    ordered = []
    for run, first_segment, anchor_unix in runs:
        ordered.append((first_segment, run, anchor_unix))
    ordered.sort()

    clock = {}
    for index, (first_segment, _, anchor_unix) in enumerate(ordered):
        if index + 1 < len(ordered):
            stop = ordered[index + 1][0]
        else:
            stop = last_segment + 1

        elapsed = 0
        for segment in range(first_segment, stop):
            if segment not in frames_by_segment:
                elapsed = None
                continue
            if elapsed is None:
                clock[segment] = None
            else:
                clock[segment] = anchor_unix + elapsed * FRAME_MS / 1000
                elapsed += frames_by_segment[segment]
    return clock

def clock_unix(clock, segment, frame):
    start = clock.get(segment)
    if start is None:
        return None
    return start + frame * FRAME_MS / 1000

def class_windows(score_rows, layouts):
    """Per class, per stretch, the (start, end, score) of each window in order.

    score_rows are (class, segment, first_frame, score), ordered by class,
    segment and frame. A window in no stretch is left out.
    """
    placement = {}
    for index, stretch in enumerate(layouts):
        for segment, offset in zip(stretch.segments, stretch.offsets):
            placement[segment] = (index, offset)

    by_class = {}
    for class_name, segment, first_frame, score in score_rows:
        if segment not in placement:
            continue
        index, offset = placement[segment]
        start = offset + first_frame
        end = min(start + WINDOW_FRAMES, layouts[index].frames)
        per_stretch = by_class.setdefault(class_name, {})
        per_stretch.setdefault(index, []).append((start, end, score))
    return by_class

def event_rows(session_id, source, class_name, stretch, events, clock):
    rows = []
    for event in events:
        start_segment, start_frame = locate(stretch, event.start)
        last_segment, last_frame = locate(stretch, event.end - 1)
        rows.append((
            session_id,
            source,
            class_name,
            start_segment,
            start_frame,
            last_segment,
            last_frame + 1,
            clock_unix(clock, start_segment, start_frame),
            clock_unix(clock, last_segment, last_frame + 1),
            (event.end - event.start) * FRAME_MS,
            event.windows,
            event.peak,
            event.total / event.windows,
        ))
    return rows

def derive_session(connection, session_id, source, rules):
    """Every event of one session and source, as events rows."""
    segment_rows = connection.execute(
        "SELECT segment, frames, corrupt FROM segments WHERE session_id = ?", [session_id]).fetchall()
    runs = connection.execute(
        "SELECT run, first_segment, anchor_unix FROM runs WHERE session_id = ?", [session_id]).fetchall()
    run_starts = set()
    for _, first_segment, _ in runs:
        run_starts.add(first_segment)

    layouts = stretch_layouts(segment_rows, run_starts)
    clock = segment_clock(segment_rows, runs)
    score_rows = connection.execute(
        "SELECT class, segment, first_frame, score FROM windows WHERE session_id = ? AND source = ? "
        "ORDER BY class, segment, first_frame", [session_id, source]).fetchall()

    rows = []
    for class_name, per_stretch in class_windows(score_rows, layouts).items():
        rule = rules.get(class_name)
        if rule is None:
            continue
        for index, windows in per_stretch.items():
            events = stretch_events(windows, rule)
            rows += event_rows(session_id, source, class_name, layouts[index], events, clock)
    return rows

def event_columns(rows):
    """events rows as one array per column, an unknown clock time as NaN."""
    columns = {}
    for position, name in enumerate(EVENT_COLUMNS):
        values = []
        for row in rows:
            value = row[position]
            if value is None:
                value = math.nan
            values.append(value)
        columns[name] = np.array(values)
    return columns

def insert_events(connection, rows):
    columns = event_columns(rows)
    selected = []
    for name in columns:
        if name in CLOCK_COLUMNS:
            selected.append(f"CASE WHEN isnan({name}) THEN NULL ELSE {name} END")
        else:
            selected.append(name)
    connection.execute(f"INSERT INTO events SELECT {', '.join(selected)} FROM columns")

def store_events(connection, session_id, source, rows, digest, scored_unix, derived_unix):
    connection.begin()
    try:
        connection.execute("DELETE FROM events WHERE session_id = ? AND source = ?", [session_id, source])
        connection.execute("DELETE FROM evented WHERE session_id = ? AND source = ?", [session_id, source])
        if rows:
            insert_events(connection, rows)
        connection.execute("INSERT INTO evented VALUES (?, ?, ?, ?, ?, ?)",
                           [session_id, source, digest, scored_unix, len(rows), derived_unix])
    except Exception:
        connection.rollback()
        raise

    connection.commit()

def sessions_to_derive(connection, digest, force):
    """(session_id, source, scored_unix) whose events are missing, or older than its scores or the rules."""
    return connection.execute(
        "SELECT scored.session_id, scored.source, scored.scored_unix FROM scored "
        "LEFT JOIN evented ON evented.session_id = scored.session_id AND evented.source = scored.source "
        "WHERE ? OR evented.session_id IS NULL OR evented.rules_sha256 <> ? "
        "OR evented.scored_unix <> scored.scored_unix "
        "ORDER BY scored.session_id, scored.source", [force, digest]).fetchall()

def class_counts(rows):
    counts = {}
    for row in rows:
        counts[row[2]] = counts.get(row[2], 0) + 1

    parts = []
    for class_name in sorted(counts):
        parts.append(f"{class_name} {counts[class_name]}")
    return ", ".join(parts)

def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--force", action="store_true", help="derive sessions whose events are current")
    parser.add_argument("--rules", type=Path, default=RULES_PATH, help=f"rules file (default {RULES_PATH.name})")
    args = parser.parse_args()

    rules = load_rules(args.rules)
    digest = rules_digest(rules)
    connection = connect()
    pending = sessions_to_derive(connection, digest, args.force)
    if not pending:
        print("events are current")
        return

    for session_id, source, scored_unix in pending:
        started = time.perf_counter()
        rows = derive_session(connection, session_id, source, rules)
        store_events(connection, session_id, source, rows, digest, scored_unix, int(time.time()))
        elapsed = time.perf_counter() - started
        print(f"events {session_id} {source}: {len(rows)} in {elapsed:.1f} s ({class_counts(rows) or 'none'})")

if __name__ == "__main__":
    main()
