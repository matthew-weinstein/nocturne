#pragma once

#include <stdint.h>

typedef enum {
    SEGMENT_CURSOR_CONTINUE,
    SEGMENT_CURSOR_FLUSH_CHUNK,
    SEGMENT_CURSOR_CLOSE_SEGMENT,
} segment_cursor_action_t;

typedef struct {
    uint32_t frames_per_segment;
    uint32_t frames_in_segment;
    uint32_t frames_in_chunk;
} segment_cursor_t;

void segment_cursor_reset(segment_cursor_t *cursor, uint32_t frames_per_segment);
segment_cursor_action_t segment_cursor_add_frame(segment_cursor_t *cursor);
