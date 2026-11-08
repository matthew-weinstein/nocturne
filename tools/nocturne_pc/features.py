"""YAMNet's log-mel front end, the reference for audio_features.c (design doc §21).

A patch is 96 frames of a 25 ms periodic Hann window every 10 ms, a 512-point
FFT magnitude, triangular mel bands on the HTK scale, and log(mel + 0.001),
as tf.signal.stft and tf.signal.linear_to_mel_weight_matrix compute them.

96 frames span 15,600 samples (0.975 s), so a patch for a 0.96 s window
reads 240 samples into the next one. Samples are 16-bit PCM scaled by
1/32768, as YAMNet reads a WAV.
"""

import functools

import numpy as np

SAMPLE_RATE_HZ = 16000
WINDOW_SAMPLES = 400
HOP_SAMPLES = 160
FFT_SIZE = 512
NUM_BINS = FFT_SIZE // 2 + 1
PATCH_FRAMES = 96
PATCH_SAMPLES = (PATCH_FRAMES - 1) * HOP_SAMPLES + WINDOW_SAMPLES
LOG_OFFSET = 0.001
PCM_FULL_SCALE = 32768.0

YAMNET_BANDS = 64
YAMNET_MIN_HZ = 125.0
YAMNET_MAX_HZ = 7500.0

MEL_BREAK_HZ = 700.0
MEL_HIGH_Q = 1127.0

def hann_window():
    """Periodic Hann, as tf.signal.hann_window(periodic=True)."""
    positions = np.arange(WINDOW_SAMPLES) / WINDOW_SAMPLES
    return 0.5 - 0.5 * np.cos(2 * np.pi * positions)

def hertz_to_mel(hertz):
    return MEL_HIGH_Q * np.log(1.0 + hertz / MEL_BREAK_HZ)

@functools.cache
def mel_weight_matrix(num_bands=YAMNET_BANDS, min_hz=YAMNET_MIN_HZ, max_hz=YAMNET_MAX_HZ):
    """(NUM_BINS, num_bands) triangular weights; the DC bin is always zero."""
    nyquist_hz = SAMPLE_RATE_HZ / 2
    bin_hz = np.linspace(0.0, nyquist_hz, NUM_BINS)[1:]
    bin_mel = hertz_to_mel(bin_hz)[:, np.newaxis]

    edges_mel = np.linspace(hertz_to_mel(min_hz), hertz_to_mel(max_hz), num_bands + 2)
    lower_mel = edges_mel[:-2]
    center_mel = edges_mel[1:-1]
    upper_mel = edges_mel[2:]

    rising = (bin_mel - lower_mel) / (center_mel - lower_mel)
    falling = (upper_mel - bin_mel) / (upper_mel - center_mel)
    weights = np.maximum(0.0, np.minimum(rising, falling))

    dc_row = np.zeros((1, num_bands))
    return np.concatenate([dc_row, weights])

def magnitude_frames(waveform):
    """|FFT| of every full 400-sample frame at a 160-sample hop, (frames, NUM_BINS)."""
    frames = np.lib.stride_tricks.sliding_window_view(waveform, WINDOW_SAMPLES)[::HOP_SAMPLES]
    return np.abs(np.fft.rfft(frames * hann_window(), FFT_SIZE, axis=1))

def log_mel_patch(samples, num_bands=YAMNET_BANDS, min_hz=YAMNET_MIN_HZ, max_hz=YAMNET_MAX_HZ):
    """(96, num_bands) log-mel patch from PATCH_SAMPLES 16-bit samples."""
    samples = np.asarray(samples)
    return waveform_log_mel_patch(samples.astype(np.float64) / PCM_FULL_SCALE, num_bands, min_hz, max_hz)

def waveform_log_mel_patch(waveform, num_bands=YAMNET_BANDS, min_hz=YAMNET_MIN_HZ, max_hz=YAMNET_MAX_HZ):
    """(96, num_bands) log-mel patch from PATCH_SAMPLES samples at a full scale of 1.0, as audio decodes."""
    waveform = np.asarray(waveform, dtype=np.float64)
    if waveform.shape != (PATCH_SAMPLES,):
        raise ValueError(f"a patch takes {PATCH_SAMPLES} samples, not {waveform.shape}")

    mel = magnitude_frames(waveform) @ mel_weight_matrix(num_bands, min_hz, max_hz)
    return np.log(mel + LOG_OFFSET)
