#!/usr/bin/env python3
"""Write the stored vectors that hold audio_features.c to nocturne_pc.features.

    python tools/model/make_feature_vectors.py

The input is integer arithmetic that audio_features_vectors.c repeats
exactly: an 80 Hz sawtooth, a burst that clips, a 5.3 kHz tone in the second
half and LCG noise. Only the expected patches are stored, one for YAMNet's
bands and one for a 40-band, 20-8000 Hz config.
"""

import argparse
from pathlib import Path

import numpy as np

from nocturne_pc.features import PATCH_FRAMES, PATCH_SAMPLES, YAMNET_BANDS, YAMNET_MAX_HZ, YAMNET_MIN_HZ, log_mel_patch

ROOT = Path(__file__).resolve().parent.parent.parent
VECTORS_PATH = ROOT / "components" / "classifier" / "test_vectors" / "audio_features_vectors.c"

SAW_PERIOD = 200
SAW_SCALE = 80
BURST_START = 4000
BURST_END = 4800
BURST_GAIN = 5
TONE_START = 7800
TONE_PERIOD = 3
TONE_HIGH = 2000
TONE_LOW = -1000
LCG_MULTIPLIER = 1103515245
LCG_INCREMENT = 12345
LCG_MASK = 0x7FFFFFFF
NOISE_SHIFT = 16
NOISE_MASK = 0xFFF
NOISE_CENTER = 2048
PCM_MIN = -32768
PCM_MAX = 32767
VALUES_PER_LINE = 6

CONFIGS = (
    ("yamnet", YAMNET_BANDS, YAMNET_MIN_HZ, YAMNET_MAX_HZ),
    ("wide40", 40, 20.0, 8000.0),
)

C_SAMPLES_FUNCTION = f"""void audio_features_vector_samples(int16_t *samples) {{
    uint32_t state = 1;

    for (int n = 0; n < AUDIO_FEATURES_PATCH_SAMPLES; n++) {{
        int32_t saw = (n % {SAW_PERIOD} - {SAW_PERIOD // 2}) * {SAW_SCALE};
        if (n >= {BURST_START} && n < {BURST_END}) {{
            saw *= {BURST_GAIN};
        }}

        int32_t tone = 0;
        if (n >= {TONE_START}) {{
            tone = n % {TONE_PERIOD} == 0 ? {TONE_HIGH} : {TONE_LOW};
        }}

        state = (state * {LCG_MULTIPLIER}u + {LCG_INCREMENT}u) & 0x{LCG_MASK:X}u;
        int32_t noise = (int32_t)((state >> {NOISE_SHIFT}) & 0x{NOISE_MASK:X}u) - {NOISE_CENTER};

        int32_t value = saw + tone + noise;
        if (value > INT16_MAX) {{
            value = INT16_MAX;
        }}
        if (value < INT16_MIN) {{
            value = INT16_MIN;
        }}
        samples[n] = (int16_t)value;
    }}
}}
"""

def vector_samples():
    samples = np.zeros(PATCH_SAMPLES, dtype=np.int16)
    state = 1

    for n in range(PATCH_SAMPLES):
        saw = (n % SAW_PERIOD - SAW_PERIOD // 2) * SAW_SCALE
        if BURST_START <= n < BURST_END:
            saw *= BURST_GAIN

        tone = 0
        if n >= TONE_START:
            if n % TONE_PERIOD == 0:
                tone = TONE_HIGH
            else:
                tone = TONE_LOW

        state = (state * LCG_MULTIPLIER + LCG_INCREMENT) & LCG_MASK
        noise = ((state >> NOISE_SHIFT) & NOISE_MASK) - NOISE_CENTER

        value = saw + tone + noise
        value = min(value, PCM_MAX)
        value = max(value, PCM_MIN)
        samples[n] = value

    return samples

def c_float_array(name, values):
    lines = [f"const float {name}[{len(values)}] = {{"]

    for start in range(0, len(values), VALUES_PER_LINE):
        texts = []
        for value in values[start:start + VALUES_PER_LINE]:
            texts.append(f"{value:.7e}f")
        lines.append("    " + ", ".join(texts) + ",")

    lines.append("};")
    return "\n".join(lines) + "\n"

def vectors_source():
    samples = vector_samples()
    parts = ['#include "audio_features_vectors.h"\n', C_SAMPLES_FUNCTION]

    for name, num_bands, min_hz, max_hz in CONFIGS:
        patch = log_mel_patch(samples, num_bands, min_hz, max_hz)
        assert patch.shape == (PATCH_FRAMES, num_bands)

        parts.append(f"const audio_features_config_t audio_features_vector_{name}_config = "
                     f"{{{num_bands}, {min_hz:.1f}f, {max_hz:.1f}f}};\n")
        parts.append(c_float_array(f"audio_features_vector_{name}_patch", patch.reshape(-1)))

    return "\n".join(parts)

def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--out", type=Path, default=VECTORS_PATH, help="where to write the C vectors")
    args = parser.parse_args()

    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(vectors_source(), encoding="utf-8", newline="\n")
    print(f"wrote {args.out}")

if __name__ == "__main__":
    main()
