#include "wav_writer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WAV_HEADER_SIZE 44
#define BITS_PER_SAMPLE 16
#define NUM_CHANNELS    1

struct wav_writer {
    FILE *file;
    uint32_t sample_rate;
    uint32_t num_samples_written;
};

static void write_u32_le(uint8_t *dest, uint32_t value)
{
    dest[0] = (uint8_t)(value);
    dest[1] = (uint8_t)(value >> 8);
    dest[2] = (uint8_t)(value >> 16);
    dest[3] = (uint8_t)(value >> 24);
}

static void write_u16_le(uint8_t *dest, uint16_t value)
{
    dest[0] = (uint8_t)(value);
    dest[1] = (uint8_t)(value >> 8);
}

static void build_header(uint8_t *header, uint32_t sample_rate, uint32_t num_samples)
{
    const uint32_t data_bytes = num_samples * NUM_CHANNELS * (BITS_PER_SAMPLE / 8);
    const uint32_t byte_rate  = sample_rate * NUM_CHANNELS * (BITS_PER_SAMPLE / 8);
    const uint16_t block_align = NUM_CHANNELS * (BITS_PER_SAMPLE / 8);

    memcpy(header + 0, "RIFF", 4);
    write_u32_le(header + 4, 36 + data_bytes);
    memcpy(header + 8, "WAVE", 4);

    memcpy(header + 12, "fmt ", 4);
    write_u32_le(header + 16, 16);              /* subchunk size for PCM */
    write_u16_le(header + 20, 1);               /* format: PCM */
    write_u16_le(header + 22, NUM_CHANNELS);
    write_u32_le(header + 24, sample_rate);
    write_u32_le(header + 28, byte_rate);
    write_u16_le(header + 32, block_align);
    write_u16_le(header + 34, BITS_PER_SAMPLE);

    memcpy(header + 36, "data", 4);
    write_u32_le(header + 40, data_bytes);
}

esp_err_t wav_writer_open(wav_writer_t **writer, const char *path, uint32_t sample_rate)
{
    wav_writer_t *new_writer = calloc(1, sizeof(wav_writer_t));
    if (new_writer == NULL) {
        return ESP_ERR_NO_MEM;
    }

    new_writer->file = fopen(path, "wb");
    if (new_writer->file == NULL) {
        free(new_writer);
        return ESP_FAIL;
    }

    new_writer->sample_rate = sample_rate;
    new_writer->num_samples_written = 0;

    uint8_t header[WAV_HEADER_SIZE];
    build_header(header, sample_rate, 0);

    if (fwrite(header, 1, WAV_HEADER_SIZE, new_writer->file) != WAV_HEADER_SIZE) {
        fclose(new_writer->file);
        free(new_writer);
        return ESP_FAIL;
    }

    *writer = new_writer;
    return ESP_OK;
}

esp_err_t wav_writer_write(wav_writer_t *writer, const int16_t *samples, size_t num_samples)
{
    size_t written = fwrite(samples, sizeof(int16_t), num_samples, writer->file);
    if (written != num_samples) {
        return ESP_FAIL;
    }

    writer->num_samples_written += num_samples;
    return ESP_OK;
}

esp_err_t wav_writer_close(wav_writer_t *writer)
{
    uint8_t header[WAV_HEADER_SIZE];
    build_header(header, writer->sample_rate, writer->num_samples_written);

    esp_err_t status = ESP_OK;

    if (fseek(writer->file, 0, SEEK_SET) != 0 ||
        fwrite(header, 1, WAV_HEADER_SIZE, writer->file) != WAV_HEADER_SIZE) {
        status = ESP_FAIL;
    }

    fclose(writer->file);
    free(writer);
    return status;
}