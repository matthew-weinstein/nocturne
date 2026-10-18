#include "system_check_fault.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "system_check.h"

#define CARD_FULL_FREE_PCT     3
#define ENCODER_STALL_MS       1000
#define WRITE_GLITCH_FAILURES  1
#define WRITE_ERROR_FAILURES   4

typedef enum {
    FAULT_NONE,
    FAULT_CARD_FULL,
    FAULT_SD_WRITE_GLITCH,
    FAULT_SD_WRITE_ERROR,
    FAULT_MIC_SILENT,
    FAULT_ENCODER_ERROR,
    FAULT_ENCODER_STALL,
    FAULT_UPLOAD_FAILING,
    FAULT_WIFI_DOWN_AT_START,
} fault_t;

static const fault_t fault =
#if CONFIG_NOCTURNE_SYSTEM_CHECK_FAULT_CARD_FULL
    FAULT_CARD_FULL;
#elif CONFIG_NOCTURNE_SYSTEM_CHECK_FAULT_SD_WRITE_GLITCH
    FAULT_SD_WRITE_GLITCH;
#elif CONFIG_NOCTURNE_SYSTEM_CHECK_FAULT_SD_WRITE_ERROR
    FAULT_SD_WRITE_ERROR;
#elif CONFIG_NOCTURNE_SYSTEM_CHECK_FAULT_MIC_SILENT
    FAULT_MIC_SILENT;
#elif CONFIG_NOCTURNE_SYSTEM_CHECK_FAULT_ENCODER_ERROR
    FAULT_ENCODER_ERROR;
#elif CONFIG_NOCTURNE_SYSTEM_CHECK_FAULT_ENCODER_STALL
    FAULT_ENCODER_STALL;
#elif CONFIG_NOCTURNE_SYSTEM_CHECK_FAULT_UPLOAD_FAILING
    FAULT_UPLOAD_FAILING;
#elif CONFIG_NOCTURNE_SYSTEM_CHECK_FAULT_WIFI_DOWN_AT_START
    FAULT_WIFI_DOWN_AT_START;
#else
    FAULT_NONE;
#endif

static volatile int fsync_failures_left;
static volatile bool encoder_fault_left;

int __real_fsync(int fd);
esp_err_t __real_i2s_microphone_read(int32_t *out_samples, size_t max_samples, size_t *num_samples);
esp_err_t __real_audio_encoder_encode_frame(const int16_t *pcm, uint8_t *out_bytes, size_t max_bytes,
                                            size_t *num_bytes);
esp_err_t __real_sd_card_free_pct(uint32_t *out_pct);
esp_err_t __real_b2_client_upload_file(const char *path, const char *file_name);

static void announce(const char *what) {
    printf("NCT_FAULT inject=%s\n", what);
}

int __wrap_fsync(int fd) {
    if (fsync_failures_left > 0) {
        fsync_failures_left--;
        announce("fsync");
        errno = EIO;
        return -1;
    }
    return __real_fsync(fd);
}

esp_err_t __wrap_i2s_microphone_read(int32_t *out_samples, size_t max_samples, size_t *num_samples) {
    esp_err_t status = __real_i2s_microphone_read(out_samples, max_samples, num_samples);
    if (fault == FAULT_MIC_SILENT && status == ESP_OK) {
        memset(out_samples, 0, *num_samples * sizeof(out_samples[0]));
    }
    return status;
}

esp_err_t __wrap_audio_encoder_encode_frame(const int16_t *pcm, uint8_t *out_bytes, size_t max_bytes,
                                            size_t *num_bytes) {
    if (encoder_fault_left) {
        encoder_fault_left = false;
        if (fault == FAULT_ENCODER_ERROR) {
            announce("encoder error");
            return ESP_FAIL;
        }
        announce("encoder stall");
        vTaskDelay(pdMS_TO_TICKS(ENCODER_STALL_MS));
    }
    return __real_audio_encoder_encode_frame(pcm, out_bytes, max_bytes, num_bytes);
}

esp_err_t __wrap_sd_card_free_pct(uint32_t *out_pct) {
    esp_err_t status = __real_sd_card_free_pct(out_pct);
    if (fault == FAULT_CARD_FULL && status == ESP_OK) {
        *out_pct = CARD_FULL_FREE_PCT;
    }
    return status;
}

esp_err_t __wrap_b2_client_upload_file(const char *path, const char *file_name) {
    if (fault == FAULT_UPLOAD_FAILING) {
        announce("upload failure");
        return ESP_FAIL;
    }
    return __real_b2_client_upload_file(path, file_name);
}

void system_check_fault_before_start(void) {
    switch (fault) {
    case FAULT_CARD_FULL:
        announce("card full");
        break;
    case FAULT_MIC_SILENT:
        announce("mic silent");
        break;
    case FAULT_WIFI_DOWN_AT_START:
        system_check_wifi_outage(true);
        break;
    default:
        break;
    }
}

void system_check_fault_arm(void) {
    switch (fault) {
    case FAULT_SD_WRITE_GLITCH:
        fsync_failures_left = WRITE_GLITCH_FAILURES;
        break;
    case FAULT_SD_WRITE_ERROR:
        fsync_failures_left = WRITE_ERROR_FAILURES;
        break;
    case FAULT_ENCODER_ERROR:
    case FAULT_ENCODER_STALL:
        encoder_fault_left = true;
        break;
    default:
        break;
    }
}

void system_check_fault_before_stop(void) {
    if (fault == FAULT_WIFI_DOWN_AT_START) {
        system_check_wifi_outage(false);
    }
}

bool system_check_fault_injected(void) {
    return fault != FAULT_NONE;
}
