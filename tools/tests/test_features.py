import numpy as np
import pytest

from model.make_feature_vectors import VECTORS_PATH, vector_samples, vectors_source
from nocturne_pc.features import (
    HOP_SAMPLES,
    LOG_OFFSET,
    NUM_BINS,
    PATCH_FRAMES,
    PATCH_SAMPLES,
    SAMPLE_RATE_HZ,
    WINDOW_SAMPLES,
    YAMNET_BANDS,
    YAMNET_MAX_HZ,
    YAMNET_MIN_HZ,
    hertz_to_mel,
    log_mel_patch,
    mel_weight_matrix,
)

TONE_AMPLITUDE = 16000

def tone(hertz):
    times = np.arange(PATCH_SAMPLES) / SAMPLE_RATE_HZ
    return np.round(TONE_AMPLITUDE * np.sin(2 * np.pi * hertz * times)).astype(np.int16)

def band_centers_hz(num_bands, min_hz, max_hz):
    edges_mel = np.linspace(hertz_to_mel(min_hz), hertz_to_mel(max_hz), num_bands + 2)
    centers_mel = edges_mel[1:-1]
    return 700.0 * (np.exp(centers_mel / 1127.0) - 1.0)

def test_a_patch_is_96_frames_of_64_bands_from_0_975_s():
    assert PATCH_SAMPLES == 15600
    assert (PATCH_FRAMES - 1) * HOP_SAMPLES + WINDOW_SAMPLES == PATCH_SAMPLES
    assert log_mel_patch(np.zeros(PATCH_SAMPLES, dtype=np.int16)).shape == (PATCH_FRAMES, YAMNET_BANDS)

def test_the_mel_matrix_has_no_dc_and_peaks_at_most_one():
    weights = mel_weight_matrix()

    assert weights.shape == (NUM_BINS, YAMNET_BANDS)
    assert np.all(weights[0] == 0.0)
    assert np.all(weights >= 0.0)
    assert np.all(weights.max(axis=0) <= 1.0)
    assert np.all(weights.max(axis=0) > 0.0)

def test_silence_is_the_log_offset():
    patch = log_mel_patch(np.zeros(PATCH_SAMPLES, dtype=np.int16))

    assert np.allclose(patch, np.log(LOG_OFFSET))

def test_a_tone_is_loudest_in_the_band_centred_nearest_it():
    centers = band_centers_hz(YAMNET_BANDS, YAMNET_MIN_HZ, YAMNET_MAX_HZ)

    for hertz in (250.0, 1000.0, 4000.0):
        patch = log_mel_patch(tone(hertz))
        loudest = int(np.argmax(patch.mean(axis=0)))
        nearest = int(np.argmin(np.abs(centers - hertz)))
        assert abs(loudest - nearest) <= 1

def test_a_patch_takes_exactly_its_samples():
    with pytest.raises(ValueError, match="15600"):
        log_mel_patch(np.zeros(PATCH_SAMPLES - 1, dtype=np.int16))

def test_the_vector_signal_clips_and_uses_the_whole_range():
    samples = vector_samples()

    assert samples.max() == 32767
    assert samples.min() == -32768
    assert samples.dtype == np.int16

def test_the_stored_c_vectors_match_the_reference():
    assert VECTORS_PATH.read_text(encoding="utf-8") == vectors_source()

def test_the_reference_matches_tensorflow():
    tf = pytest.importorskip("tensorflow")

    expected_weights = tf.signal.linear_to_mel_weight_matrix(
        num_mel_bins=YAMNET_BANDS, num_spectrogram_bins=NUM_BINS, sample_rate=SAMPLE_RATE_HZ,
        lower_edge_hertz=YAMNET_MIN_HZ, upper_edge_hertz=YAMNET_MAX_HZ).numpy()
    np.testing.assert_allclose(mel_weight_matrix(), expected_weights, atol=1e-5)

    samples = vector_samples()
    waveform = tf.constant(samples.astype(np.float32) / 32768.0)
    magnitude = tf.abs(tf.signal.stft(waveform, frame_length=WINDOW_SAMPLES, frame_step=HOP_SAMPLES,
                                      fft_length=512))
    expected_patch = tf.math.log(tf.matmul(magnitude, expected_weights) + LOG_OFFSET).numpy()

    assert expected_patch.shape == (PATCH_FRAMES, YAMNET_BANDS)
    np.testing.assert_allclose(log_mel_patch(samples), expected_patch, atol=1e-4)
