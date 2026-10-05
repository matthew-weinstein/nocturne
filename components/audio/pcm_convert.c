#include "pcm_convert.h"

void pcm_convert_from_i2s(const int32_t *samples, int16_t *out_pcm, size_t num_samples) {
    for (size_t i = 0; i < num_samples; i++) {
        out_pcm[i] = (int16_t)(samples[i] >> PCM_CONVERT_SHIFT);
    }
}
