"""Score every 0.96 s window of each ingested session with float YAMNet (design doc 9.11).

    cd tools
    python -m nocturne_pc.teacher
    python -m nocturne_pc.teacher --force

A session is scored once it is ingested (9.5) and still in audio/; --force
rescores every one. Its audio is decoded a segment at a time and cut into
windows of 48 frames, each read as a 0.975 s log-mel patch (240 samples into
the next window, zero-padded at the end of a stretch). A stretch is audio
with no break: a resume, a missing or corrupt segment starts a new one, and
its windows restart there. A window holding any muted frame is not scored,
as the board would not run it.

Each window gives the YAMNet class groups (design doc §21) into windows, the
top five of 521 classes into teacher, and RMS and peak in dBFS into levels.
A session is replaced whole in one transaction.
"""

import argparse
import sys
import time
from collections import namedtuple

import numpy as np

from nocturne_pc import data_root
from nocturne_pc.audio import FRAME_SAMPLES, SessionAudio, device_key, read_passphrase
from nocturne_pc.db import connect
from nocturne_pc.features import PATCH_SAMPLES, waveform_log_mel_patch
from nocturne_pc.yamnet import Yamnet, class_names, group_members, group_scores

SOURCE = "pc:yamnet-float"
WINDOW_FRAMES = 48
WINDOW_SAMPLES = WINDOW_FRAMES * FRAME_SAMPLES
FRAME_MS = 20
TOP_CLASSES = 5
LEVEL_FLOOR_DBFS = -127.5
BATCH_WINDOWS = 64

Window = namedtuple("Window", ["segment", "first_frame", "patch", "frames"])

def stretches(segment_rows, run_starts, available):
    """Segments in runs of unbroken audio, and the corrupt ones left out.

    segment_rows are (segment, frames, corrupt). A run start, a missing
    segment or a left-out one ends the stretch before it.
    """
    result = []
    corrupt = []
    current = []
    previous = None

    for segment, frames, is_corrupt in sorted(segment_rows):
        if is_corrupt:
            corrupt.append(segment)
        usable = not is_corrupt and frames > 0 and segment in available
        continues = previous is not None and segment == previous + 1 and segment not in run_starts

        if current and not (usable and continues):
            result.append(current)
            current = []
        if usable:
            current.append(segment)
            previous = segment
        else:
            previous = None

    if current:
        result.append(current)
    return result, corrupt

def stretch_windows(audio, segments):
    """Every window of one stretch, in order; the last is zero-padded to a whole patch."""
    pending = np.zeros(0, dtype=np.float32)
    origins = []

    for segment in segments:
        samples = audio.decode(segment)
        pending = np.concatenate([pending, samples])
        for frame in range(len(samples) // FRAME_SAMPLES):
            origins.append((segment, frame))

        cursor = 0
        while len(origins) - cursor > WINDOW_FRAMES:
            start = cursor * FRAME_SAMPLES
            frames = origins[cursor:cursor + WINDOW_FRAMES]
            yield Window(*frames[0], pending[start:start + PATCH_SAMPLES], frames)
            cursor += WINDOW_FRAMES

        pending = pending[cursor * FRAME_SAMPLES:]
        origins = origins[cursor:]

    cursor = 0
    while cursor < len(origins):
        start = cursor * FRAME_SAMPLES
        patch = np.zeros(PATCH_SAMPLES, dtype=np.float32)
        tail = pending[start:start + PATCH_SAMPLES]
        patch[:len(tail)] = tail
        frames = origins[cursor:cursor + WINDOW_FRAMES]
        yield Window(*frames[0], patch, frames)
        cursor += WINDOW_FRAMES

def muted_frames(mute_rows):
    """(segment, frame) of every muted frame, from mutes rows (segment, start_ms, ms)."""
    muted = set()
    for segment, start_ms, ms in mute_rows:
        first = start_ms // FRAME_MS
        for frame in range(first, first + ms // FRAME_MS):
            muted.add((segment, frame))
    return muted

def is_muted(window, muted):
    for frame in window.frames:
        if frame in muted:
            return True
    return False

def level_dbfs(value):
    if value <= 0.0:
        return LEVEL_FLOOR_DBFS
    return max(20.0 * np.log10(value), LEVEL_FLOOR_DBFS)

def window_levels(window):
    """RMS and peak in dBFS over the window's own samples, not the patch's look-ahead."""
    samples = window.patch[:len(window.frames) * FRAME_SAMPLES].astype(np.float64)
    rms = np.sqrt(np.mean(samples * samples))
    peak = np.max(np.abs(samples))
    return level_dbfs(rms), level_dbfs(peak)

class SessionScores:
    """Per-window results gathered for one session, then stored as columns."""

    def __init__(self, session_id, groups, names):
        self.session_id = session_id
        self.groups = groups
        self.names = names
        self.segments = []
        self.first_frames = []
        self.group_scores = []
        self.top_classes = []
        self.top_scores = []
        self.levels = []

    def add(self, windows, scores, members):
        for window in windows:
            self.segments.append(window.segment)
            self.first_frames.append(window.first_frame)
            self.levels.append(window_levels(window))

        self.group_scores.append(group_scores(scores, members))
        top = np.argsort(-scores, axis=1, kind="stable")[:, :TOP_CLASSES]
        self.top_classes.append(top)
        self.top_scores.append(np.take_along_axis(scores, top, axis=1))

    def num_windows(self):
        return len(self.segments)

    def columns(self):
        num_windows = self.num_windows()
        segments = np.array(self.segments, dtype=np.int32)
        first_frames = np.array(self.first_frames, dtype=np.int32)
        num_groups = len(self.groups)
        class_names_array = np.array(self.names)

        if num_windows == 0:
            grouped = np.zeros((0, num_groups), dtype=np.float32)
            top_classes = np.zeros((0, TOP_CLASSES), dtype=np.int64)
            top_scores = np.zeros((0, TOP_CLASSES), dtype=np.float32)
            levels = np.zeros((0, 2))
        else:
            grouped = np.concatenate(self.group_scores)
            top_classes = np.concatenate(self.top_classes)
            top_scores = np.concatenate(self.top_scores)
            levels = np.array(self.levels)

        windows = {
            "session_id": np.full(num_windows * num_groups, self.session_id),
            "source": np.full(num_windows * num_groups, SOURCE),
            "segment": np.repeat(segments, num_groups),
            "first_frame": np.repeat(first_frames, num_groups),
            "class": np.tile(np.array(self.groups), num_windows),
            "score": grouped.astype(np.float32).reshape(-1),
        }
        teacher = {
            "session_id": np.full(num_windows * TOP_CLASSES, self.session_id),
            "segment": np.repeat(segments, TOP_CLASSES),
            "first_frame": np.repeat(first_frames, TOP_CLASSES),
            "rank": np.tile(np.arange(1, TOP_CLASSES + 1, dtype=np.int32), num_windows),
            "yamnet_class": class_names_array[top_classes.reshape(-1)],
            "score": top_scores.astype(np.float32).reshape(-1),
        }
        levels_columns = {
            "session_id": np.full(num_windows, self.session_id),
            "segment": segments,
            "first_frame": first_frames,
            "rms_dbfs": levels[:, 0].astype(np.float32),
            "peak_dbfs": levels[:, 1].astype(np.float32),
        }
        return windows, teacher, levels_columns

def store_scores(connection, scores, num_muted, scored_unix):
    windows, teacher, levels = scores.columns()
    session_id = scores.session_id

    connection.begin()
    try:
        connection.execute("DELETE FROM windows WHERE session_id = ? AND source = ?", [session_id, SOURCE])
        connection.execute("DELETE FROM teacher WHERE session_id = ?", [session_id])
        connection.execute("DELETE FROM levels WHERE session_id = ?", [session_id])
        connection.execute("DELETE FROM scored WHERE session_id = ? AND source = ?", [session_id, SOURCE])

        if scores.num_windows() > 0:
            insert_columns(connection, "windows", windows)
            insert_columns(connection, "teacher", teacher)
            insert_columns(connection, "levels", levels)
        connection.execute("INSERT INTO scored VALUES (?, ?, ?, ?, ?)",
                           [session_id, SOURCE, scores.num_windows(), num_muted, scored_unix])
    except Exception:
        connection.rollback()
        raise

    connection.commit()

def insert_columns(connection, table, columns):
    quoted = []
    for name in columns:
        quoted.append(f'"{name}"')
    connection.execute(f"INSERT INTO {table} ({', '.join(quoted)}) SELECT * FROM columns")

def score_session(connection, model, session_id, session_dir, key, scored_unix):
    segment_rows = connection.execute(
        "SELECT segment, frames, corrupt FROM segments WHERE session_id = ?", [session_id]).fetchall()
    run_starts = set()
    for (first_segment,) in connection.execute(
            "SELECT first_segment FROM runs WHERE session_id = ?", [session_id]).fetchall():
        run_starts.add(first_segment)
    muted = muted_frames(connection.execute(
        "SELECT segment, start_ms, ms FROM mutes WHERE session_id = ?", [session_id]).fetchall())

    audio = SessionAudio(session_dir, key)
    session_stretches, corrupt = stretches(segment_rows, run_starts, set(audio.segments()))

    names = class_names()
    members = group_members(names)
    scores = SessionScores(session_id, list(members), names)
    num_muted = 0
    batch = []

    def flush():
        patches = []
        for window in batch:
            patches.append(waveform_log_mel_patch(window.patch))
        scores.add(batch, model.scores(np.array(patches, dtype=np.float32)), members)
        batch.clear()

    for stretch in session_stretches:
        for window in stretch_windows(audio, stretch):
            if is_muted(window, muted):
                num_muted += 1
                continue
            batch.append(window)
            if len(batch) == BATCH_WINDOWS:
                flush()
    if batch:
        flush()

    store_scores(connection, scores, num_muted, scored_unix)
    return scores.num_windows(), num_muted, len(session_stretches), corrupt

def sessions_to_score(connection, audio_dir, force):
    rows = connection.execute(
        "SELECT session_id, device_id FROM sessions "
        "WHERE ? OR session_id NOT IN (SELECT session_id FROM scored WHERE source = ?) "
        "ORDER BY session_id", [force, SOURCE]).fetchall()

    picked = []
    for session_id, device_id in rows:
        if (audio_dir / session_id).is_dir():
            picked.append((session_id, device_id))
    return picked

def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--force", action="store_true", help="rescore sessions already scored")
    args = parser.parse_args()

    audio_dir = data_root() / "audio"
    connection = connect()
    sessions = sessions_to_score(connection, audio_dir, args.force)
    if not sessions:
        print("nothing to score")
        return

    passphrase = read_passphrase()
    model = Yamnet.load()

    failed = False
    for session_id, device_id in sessions:
        started = time.perf_counter()
        try:
            key = device_key(device_id, passphrase)
            num_windows, num_muted, num_stretches, corrupt = score_session(
                connection, model, session_id, audio_dir / session_id, key, int(time.time()))
        except ValueError as error:
            print(f"FAIL: {session_id}: {error}")
            failed = True
            continue

        elapsed = time.perf_counter() - started
        print(f"scored {session_id}: {num_windows} windows, {num_muted} muted, "
              f"{num_stretches} stretches, {elapsed:.0f} s")
        for segment in corrupt:
            print(f"  WARNING: segment {segment} is corrupt and was not scored")

    if failed:
        sys.exit(1)

if __name__ == "__main__":
    main()
