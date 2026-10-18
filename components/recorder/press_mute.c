#include "press_mute.h"

#include "audio_format.h"

#define SAMPLES_PER_MS (AUDIO_FORMAT_SAMPLE_RATE_HZ / 1000)

void press_mute_reset(press_mute_t *mute) {
    mute->num_windows = 0;
}

void press_mute_press(press_mute_t *mute, uint32_t sample) {
    uint32_t before = PRESS_MUTE_BEFORE_MS * SAMPLES_PER_MS;
    uint32_t from = 0;
    if (sample > before) {
        from = sample - before;
    }

    if (mute->num_windows > 0) {
        press_mute_window_t *last = &mute->windows[mute->num_windows - 1];
        if (last->open || from <= last->until || mute->num_windows == PRESS_MUTE_MAX_WINDOWS) {
            last->open = true;
            return;
        }
    }

    mute->windows[mute->num_windows] = (press_mute_window_t){ .from = from, .until = from, .open = true };
    mute->num_windows++;
}

void press_mute_release(press_mute_t *mute, uint32_t sample) {
    if (mute->num_windows == 0) {
        return;
    }

    press_mute_window_t *last = &mute->windows[mute->num_windows - 1];
    if (!last->open) {
        return;
    }
    last->open = false;
    last->until = sample + PRESS_MUTE_AFTER_MS * SAMPLES_PER_MS;
}

static bool is_muted(const press_mute_t *mute, uint32_t sample) {
    for (int i = 0; i < mute->num_windows; i++) {
        const press_mute_window_t *window = &mute->windows[i];
        if (sample >= window->from && (window->open || sample < window->until)) {
            return true;
        }
    }
    return false;
}

static void drop_finished(press_mute_t *mute, uint32_t next_sample) {
    int num_finished = 0;
    while (num_finished < mute->num_windows && !mute->windows[num_finished].open
           && mute->windows[num_finished].until <= next_sample) {
        num_finished++;
    }

    for (int i = num_finished; i < mute->num_windows; i++) {
        mute->windows[i - num_finished] = mute->windows[i];
    }
    mute->num_windows -= num_finished;
}

size_t press_mute_apply(press_mute_t *mute, uint32_t first_sample, int16_t *samples, size_t num_samples) {
    size_t num_muted = 0;
    for (size_t i = 0; i < num_samples; i++) {
        if (is_muted(mute, first_sample + (uint32_t)i)) {
            samples[i] = 0;
            num_muted++;
        }
    }

    drop_finished(mute, first_sample + (uint32_t)num_samples);
    return num_muted;
}
