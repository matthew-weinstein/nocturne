#include "segment_cursor.h"

#include "segment_format.h"

void segment_cursor_reset(segment_cursor_t *cursor, uint32_t frames_per_segment) {
    cursor->frames_per_segment = frames_per_segment;
    cursor->frames_in_segment = 0;
    cursor->frames_in_chunk = 0;
}

segment_cursor_action_t segment_cursor_add_frame(segment_cursor_t *cursor) {
    cursor->frames_in_segment++;
    cursor->frames_in_chunk++;

    if (cursor->frames_in_segment >= cursor->frames_per_segment) {
        return SEGMENT_CURSOR_CLOSE_SEGMENT;
    }

    if (cursor->frames_in_chunk >= SEGMENT_FORMAT_FRAMES_PER_CHUNK) {
        cursor->frames_in_chunk = 0;
        return SEGMENT_CURSOR_FLUSH_CHUNK;
    }
    return SEGMENT_CURSOR_CONTINUE;
}
