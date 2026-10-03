#include "ring_buffer.h"

#include <string.h>
#include "esp_heap_caps.h"

static int16_t *buffer;
static size_t capacity;
static size_t write_index;
static size_t read_index;
static size_t num_stored;

esp_err_t ring_buffer_init(size_t capacity_samples) {
    buffer = heap_caps_malloc(capacity_samples * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    if (buffer == NULL) {
        return ESP_ERR_NO_MEM;
    }

    capacity = capacity_samples;
    write_index = 0;
    read_index = 0;
    num_stored = 0;

    return ESP_OK;
}

esp_err_t ring_buffer_write(const int16_t *samples, size_t num_samples) {
    if (num_samples > capacity - num_stored) {
        return ESP_ERR_NO_MEM;
    }

    size_t until_wrap = capacity - write_index;

    if (num_samples <= until_wrap) {
        memcpy(&buffer[write_index], samples, num_samples * sizeof(int16_t));
    } else {
        memcpy(&buffer[write_index], samples, until_wrap * sizeof(int16_t));
        memcpy(&buffer[0], &samples[until_wrap], (num_samples - until_wrap) * sizeof(int16_t));
    }

    write_index = (write_index + num_samples) % capacity;
    num_stored += num_samples;

    return ESP_OK;
}

esp_err_t ring_buffer_read(int16_t *out_samples, size_t num_samples) {
    if (num_samples > num_stored) {
        return ESP_ERR_INVALID_SIZE;
    }

    size_t until_wrap = capacity - read_index;

    if (num_samples <= until_wrap) {
        memcpy(out_samples, &buffer[read_index], num_samples * sizeof(int16_t));
    } else {
        memcpy(out_samples, &buffer[read_index], until_wrap * sizeof(int16_t));
        memcpy(&out_samples[until_wrap], &buffer[0], (num_samples - until_wrap) * sizeof(int16_t));
    }

    read_index = (read_index + num_samples) % capacity;
    num_stored -= num_samples;

    return ESP_OK;
}

size_t ring_buffer_available(void) {
    return num_stored;
}

void ring_buffer_deinit(void) {
    heap_caps_free(buffer);
    buffer = NULL;
    capacity = 0;
    write_index = 0;
    read_index = 0;
    num_stored = 0;
}