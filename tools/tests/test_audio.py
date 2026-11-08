import json

import numpy as np
import pytest

from decrypt_chunk import TEST_PASSPHRASE, TEST_SALT_MAC
from nocturne_pc.audio import FRAME_SAMPLES, SessionAudio, decode_packets
from segment_builder import SAMPLE_RATE_HZ, encode_opus, make_segment_from_packets

SEGMENT_0_FRAMES = 100
SEGMENT_1_FRAMES = 50
TOTAL_FRAMES = SEGMENT_0_FRAMES + SEGMENT_1_FRAMES
SLICE_TOLERANCE = 2e-3

def make_signal():
    rng = np.random.default_rng(7)
    num_samples = (TOTAL_FRAMES + 2) * FRAME_SAMPLES
    times = np.arange(num_samples) / SAMPLE_RATE_HZ

    tone = 0.3 * np.sin(2 * np.pi * 440 * times)
    noise = 0.05 * rng.standard_normal(num_samples)
    return (tone + noise).astype(np.float32)

@pytest.fixture(scope="module")
def packets():
    return encode_opus(make_signal(), TOTAL_FRAMES)

@pytest.fixture(scope="module")
def reference(packets):
    return decode_packets(packets, TOTAL_FRAMES)

@pytest.fixture
def session_dir(tmp_path, kat_key, packets):
    folder = tmp_path / "audio" / "20261017_0248"
    folder.mkdir(parents=True)

    segment_0 = make_segment_from_packets(kat_key, 0, packets[:SEGMENT_0_FRAMES])
    segment_1 = make_segment_from_packets(kat_key, 1, packets[SEGMENT_0_FRAMES:])
    (folder / "seg_0000.opus.enc").write_bytes(segment_0)
    (folder / "seg_0001.opus.enc").write_bytes(segment_1)

    metadata = {"session_id": "20261017_0248", "device_id": TEST_SALT_MAC}
    (folder / "session.json").write_text(json.dumps(metadata))
    return folder

def frames_of(samples, first_frame, num_frames):
    return samples[first_frame * FRAME_SAMPLES:(first_frame + num_frames) * FRAME_SAMPLES]

def test_the_helper_gives_real_20_ms_packets(packets, reference):
    assert len(packets) == TOTAL_FRAMES
    assert len(reference) == TOTAL_FRAMES * FRAME_SAMPLES
    assert np.abs(reference).max() > 0.2

def test_a_whole_segment_matches_the_continuous_decode(session_dir, kat_key, reference):
    audio = SessionAudio(session_dir, kat_key)

    assert audio.segments() == [0, 1]
    assert audio.num_frames(0) == SEGMENT_0_FRAMES
    np.testing.assert_allclose(audio.decode(0), frames_of(reference, 0, SEGMENT_0_FRAMES), atol=1e-6)

def test_a_slice_inside_a_segment_matches_after_preroll(session_dir, kat_key, reference):
    audio = SessionAudio(session_dir, kat_key)

    samples = audio.decode(0, first_frame=40, num_frames=30)

    assert len(samples) == 30 * FRAME_SAMPLES
    np.testing.assert_allclose(samples, frames_of(reference, 40, 30), atol=SLICE_TOLERANCE)

def test_a_range_runs_on_into_the_next_segment(session_dir, kat_key, reference):
    audio = SessionAudio(session_dir, kat_key)

    samples = audio.decode(0, first_frame=90, num_frames=20)

    np.testing.assert_allclose(samples, frames_of(reference, 90, 20), atol=SLICE_TOLERANCE)

def test_a_segment_takes_its_preroll_from_the_one_before(session_dir, kat_key, reference):
    audio = SessionAudio(session_dir, kat_key)

    samples = audio.decode(1)

    assert len(samples) == SEGMENT_1_FRAMES * FRAME_SAMPLES
    np.testing.assert_allclose(samples, frames_of(reference, SEGMENT_0_FRAMES, SEGMENT_1_FRAMES),
                               atol=SLICE_TOLERANCE)

def test_ranges_outside_the_audio_are_refused(session_dir, kat_key):
    audio = SessionAudio(session_dir, kat_key)

    with pytest.raises(ValueError, match="run past"):
        audio.decode(1, first_frame=40, num_frames=20)
    with pytest.raises(ValueError, match="outside"):
        audio.decode(0, first_frame=SEGMENT_0_FRAMES + 1)
    with pytest.raises(ValueError, match="not in"):
        audio.decode(5)

def test_a_torn_tail_keeps_the_frames_before_it(session_dir, kat_key):
    path = session_dir / "seg_0001.opus.enc"
    path.write_bytes(path.read_bytes() + bytes(40))

    audio = SessionAudio(session_dir, kat_key)

    assert audio.num_frames(1) == SEGMENT_1_FRAMES

def test_open_derives_the_key_from_the_device_id(session_dir, monkeypatch):
    monkeypatch.setenv("NOCTURNE_DATA", str(session_dir.parent.parent))

    audio = SessionAudio.open("20261017_0248", passphrase=TEST_PASSPHRASE)

    assert audio.num_frames(0) == SEGMENT_0_FRAMES
