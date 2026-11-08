import json

import numpy as np
import pytest

from decrypt_chunk import TEST_PASSPHRASE, TEST_SALT_MAC
from nocturne_pc.audio import FRAME_SAMPLES
from nocturne_pc.db import connect
from nocturne_pc.features import PATCH_SAMPLES
from nocturne_pc.ingest import ingest_session
from nocturne_pc.teacher import (
    LEVEL_FLOOR_DBFS,
    SOURCE,
    TOP_CLASSES,
    WINDOW_FRAMES,
    is_muted,
    muted_frames,
    score_session,
    sessions_to_score,
    stretch_windows,
    stretches,
    window_levels,
)
from nocturne_pc.yamnet import NUM_CLASSES, class_names, group_members
from segment_builder import SAMPLE_RATE_HZ, encode_opus, make_segment_from_packets

SESSION_ID = "20261017_0248"
STARTED = 1792205292
NOW = 1792300000
SEGMENT_FRAMES = (100, 50)

class FakeAudio:
    """Segments whose samples count up across the whole stretch, so any slice says where it came from."""

    def __init__(self, frames_per_segment):
        self.samples = {}
        offset = 0
        for segment, num_frames in frames_per_segment.items():
            num_samples = num_frames * FRAME_SAMPLES
            self.samples[segment] = np.arange(offset, offset + num_samples, dtype=np.float32) + 1
            offset += num_samples

    def decode(self, segment):
        return self.samples[segment]

class RankedModel:
    """Scores every patch the same: class k scores k / 1000."""

    def scores(self, patches):
        ranks = np.arange(NUM_CLASSES, dtype=np.float32) / 1000
        return np.tile(ranks, (len(patches), 1))

def test_a_resume_a_gap_and_a_corrupt_segment_each_start_a_stretch():
    rows = [(0, 100, False), (1, 100, False), (2, 100, False), (3, 100, True), (4, 100, False),
            (6, 100, False), (7, 0, False), (8, 100, False), (9, 100, False)]

    result, corrupt = stretches(rows, run_starts={0, 2}, available={0, 1, 2, 3, 4, 6, 7, 8})

    assert result == [[0, 1], [2], [4], [6], [8]]
    assert corrupt == [3]

def test_windows_run_across_segments_and_read_240_samples_ahead():
    audio = FakeAudio({0: 100, 1: 30})
    continuous = np.concatenate([audio.samples[0], audio.samples[1]])

    windows = list(stretch_windows(audio, [0, 1]))

    assert [(window.segment, window.first_frame) for window in windows] == [(0, 0), (0, 48), (0, 96)]
    np.testing.assert_array_equal(windows[1].patch, continuous[48 * FRAME_SAMPLES:48 * FRAME_SAMPLES + PATCH_SAMPLES])
    assert windows[2].frames[4] == (1, 0)
    assert len(windows[2].frames) == 34

def test_the_last_window_of_a_stretch_is_zero_padded():
    audio = FakeAudio({0: 96})

    windows = list(stretch_windows(audio, [0]))

    assert [window.first_frame for window in windows] == [0, 48]
    last = windows[1].patch
    assert len(last) == PATCH_SAMPLES
    np.testing.assert_array_equal(last[:WINDOW_FRAMES * FRAME_SAMPLES], audio.samples[0][48 * FRAME_SAMPLES:])
    assert np.all(last[WINDOW_FRAMES * FRAME_SAMPLES:] == 0)

def test_a_window_with_any_muted_frame_is_muted():
    muted = muted_frames([(0, 1000, 200)])
    windows = list(stretch_windows(FakeAudio({0: 150}), [0]))

    assert (0, 50) in muted and (0, 59) in muted and (0, 60) not in muted
    assert [is_muted(window, muted) for window in windows] == [False, True, False, False]

def test_levels_are_dbfs_over_the_window_alone():
    audio = FakeAudio({0: 49})
    window = next(stretch_windows(audio, [0]))
    times = np.arange(PATCH_SAMPLES) / SAMPLE_RATE_HZ
    loud_lookahead = np.ones(PATCH_SAMPLES, dtype=np.float32)
    loud_lookahead[:WINDOW_FRAMES * FRAME_SAMPLES] = 0.5 * np.sin(2 * np.pi * 1000 * times[:WINDOW_FRAMES * FRAME_SAMPLES])

    rms, peak = window_levels(window._replace(patch=loud_lookahead))
    silent = window_levels(window._replace(patch=np.zeros(PATCH_SAMPLES, dtype=np.float32)))

    assert rms == pytest.approx(-9.03, abs=0.01)
    assert peak == pytest.approx(-6.02, abs=0.01)
    assert silent == (LEVEL_FLOOR_DBFS, LEVEL_FLOOR_DBFS)

def write_session(audio_dir, key, resumes=(), mutes=()):
    num_frames = sum(SEGMENT_FRAMES)
    rng = np.random.default_rng(9)
    times = np.arange((num_frames + 2) * FRAME_SAMPLES) / SAMPLE_RATE_HZ
    signal = (0.3 * np.sin(2 * np.pi * 440 * times) + 0.02 * rng.standard_normal(len(times))).astype(np.float32)
    packets = encode_opus(signal, num_frames)

    folder = audio_dir / SESSION_ID
    folder.mkdir(parents=True)
    first = 0
    for segment, frames in enumerate(SEGMENT_FRAMES):
        data = make_segment_from_packets(key, segment, packets[first:first + frames])
        (folder / f"seg_{segment:04d}.opus.enc").write_bytes(data)
        first += frames

    metadata = {
        "session_id": SESSION_ID,
        "device_id": TEST_SALT_MAC,
        "started_unix": STARTED,
        "ended_unix": STARTED + 3600,
        "segment_count": len(SEGMENT_FRAMES),
        "resumes": list(resumes),
        "muted": list(mutes),
    }
    (folder / "session.json").write_text(json.dumps(metadata))
    return folder

@pytest.fixture
def connection(tmp_path):
    database = connect(tmp_path / "nocturne.duckdb")
    yield database
    database.close()

def count(connection, table):
    return connection.execute(f"SELECT COUNT(*) FROM {table}").fetchone()[0]

def test_a_session_is_scored_into_windows_teacher_and_levels(tmp_path, kat_key, connection):
    folder = write_session(tmp_path / "audio", kat_key, mutes=[{"segment": 0, "start_ms": 1000, "ms": 200}])
    ingest_session(connection, folder, TEST_PASSPHRASE, NOW)

    num_windows, num_muted, num_stretches, corrupt = score_session(
        connection, RankedModel(), SESSION_ID, folder, kat_key, NOW)

    members = group_members(class_names())
    assert (num_windows, num_muted, num_stretches, corrupt) == (3, 1, 1, [])
    assert connection.execute("SELECT DISTINCT segment, first_frame FROM levels ORDER BY ALL").fetchall() == [
        (0, 0), (0, 96), (1, 44)]
    assert count(connection, "windows") == 3 * len(members)
    assert count(connection, "teacher") == 3 * TOP_CLASSES

    cough = connection.execute("SELECT DISTINCT source, score FROM windows WHERE class = 'cough'").fetchall()
    assert cough == [(SOURCE, pytest.approx(members["cough"].max() / 1000))]
    top = connection.execute("SELECT yamnet_class, score FROM teacher WHERE first_frame = 0 AND rank = 1").fetchall()
    assert top == [(class_names()[NUM_CLASSES - 1], pytest.approx((NUM_CLASSES - 1) / 1000))]

    rms, peak = connection.execute("SELECT rms_dbfs, peak_dbfs FROM levels WHERE first_frame = 0").fetchone()
    assert rms == pytest.approx(-13.5, abs=1.0)
    assert rms + 3.0 < peak < -6.0
    assert connection.execute("SELECT windows, muted_windows FROM scored").fetchall() == [(3, 1)]

def test_a_resume_restarts_the_windows_at_its_segment(tmp_path, kat_key, connection):
    folder = write_session(tmp_path / "audio", kat_key, resumes=[{"segment": 1, "started_unix": STARTED + 900}])
    ingest_session(connection, folder, TEST_PASSPHRASE, NOW)

    score_session(connection, RankedModel(), SESSION_ID, folder, kat_key, NOW)

    assert connection.execute("SELECT segment, first_frame FROM levels ORDER BY ALL").fetchall() == [
        (0, 0), (0, 48), (0, 96), (1, 0), (1, 48)]

def test_scoring_again_replaces_the_session(tmp_path, kat_key, connection):
    folder = write_session(tmp_path / "audio", kat_key)
    ingest_session(connection, folder, TEST_PASSPHRASE, NOW)

    score_session(connection, RankedModel(), SESSION_ID, folder, kat_key, NOW)
    score_session(connection, RankedModel(), SESSION_ID, folder, kat_key, NOW + 60)

    assert count(connection, "levels") == 4
    assert count(connection, "teacher") == 4 * TOP_CLASSES
    assert connection.execute("SELECT COUNT(*), MAX(scored_unix) FROM scored").fetchone() == (1, NOW + 60)

def test_only_unscored_sessions_still_in_audio_are_picked_unless_forced(tmp_path, kat_key, connection):
    audio_dir = tmp_path / "audio"
    folder = write_session(audio_dir, kat_key)
    ingest_session(connection, folder, TEST_PASSPHRASE, NOW)
    connection.execute("INSERT INTO sessions (session_id, device_id, started_unix, ended_unix, segment_count, "
                       "ingested_unix) VALUES ('20261001_2300', ?, 0, 0, 1, 0)", [TEST_SALT_MAC])

    assert sessions_to_score(connection, audio_dir, force=False) == [(SESSION_ID, TEST_SALT_MAC)]

    score_session(connection, RankedModel(), SESSION_ID, folder, kat_key, NOW)

    assert sessions_to_score(connection, audio_dir, force=False) == []
    assert sessions_to_score(connection, audio_dir, force=True) == [(SESSION_ID, TEST_SALT_MAC)]
