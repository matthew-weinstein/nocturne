#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PRESS_MUTE_BEFORE_MS   300
#define PRESS_MUTE_AFTER_MS    500
#define PRESS_MUTE_MAX_WINDOWS 4

typedef struct {
    uint32_t from;
    uint32_t until;
    bool open;
} press_mute_window_t;

typedef struct {
    press_mute_window_t windows[PRESS_MUTE_MAX_WINDOWS];
    int num_windows;
} press_mute_t;

void press_mute_reset(press_mute_t *mute);
void press_mute_press(press_mute_t *mute, uint32_t sample);
void press_mute_release(press_mute_t *mute, uint32_t sample);
size_t press_mute_apply(press_mute_t *mute, uint32_t first_sample, int16_t *samples, size_t num_samples);
