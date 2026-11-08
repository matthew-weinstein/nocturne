#pragma once

#include <stdint.h>
#include "audio_features.h"

#define AUDIO_FEATURES_VECTOR_YAMNET_VALUES (AUDIO_FEATURES_PATCH_FRAMES * 64)
#define AUDIO_FEATURES_VECTOR_WIDE40_VALUES (AUDIO_FEATURES_PATCH_FRAMES * 40)
#define AUDIO_FEATURES_VECTOR_TOLERANCE     1e-4f

void audio_features_vector_samples(int16_t *samples);

extern const audio_features_config_t audio_features_vector_yamnet_config;
extern const float audio_features_vector_yamnet_patch[AUDIO_FEATURES_VECTOR_YAMNET_VALUES];
extern const audio_features_config_t audio_features_vector_wide40_config;
extern const float audio_features_vector_wide40_patch[AUDIO_FEATURES_VECTOR_WIDE40_VALUES];
