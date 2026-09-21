#include "session.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "manifest.h"
#include "sd_card.h"
#include "sdkconfig.h"
#include <errno.h>

#define MAX_COLLISION_SUFFIX 99

#define SESSIONS_DIR       SD_CARD_MOUNT_POINT "/sessions"
#define SEGMENT_SECONDS CONFIG_NOCTURNE_SEGMENT_SECONDS
#define FRAME_MS           20
#define FRAMES_PER_SEGMENT ((SEGMENT_SECONDS * 1000) / FRAME_MS)

#define SESSION_DIR_LEN 64
#define PATH_LEN        128

static manifest_t manifest;
static char session_dir[SESSION_DIR_LEN];
static char manifest_path[PATH_LEN];

static FILE *segment_file;
static int frames_in_segment;
static size_t bytes_in_segment;

static void build_session_id(char *out, size_t out_len) {
    time_t now = time(NULL);
    struct tm local;
    localtime_r(&now, &local);
    strftime(out, out_len, "%Y%m%d_%H%M", &local);
}

static esp_err_t open_segment(void) {
    char filename[32];
    manifest_segment_filename(manifest.num_segments, filename, sizeof(filename));

    char path[PATH_LEN];
    snprintf(path, sizeof(path), "%s/%s", session_dir, filename);

    segment_file = fopen(path, "wb");
    if (segment_file == NULL) {
        return ESP_FAIL;
    }

    frames_in_segment = 0;
    bytes_in_segment = 0;
    return ESP_OK;
}

static esp_err_t close_segment(void) {
    if (segment_file == NULL) {
        return ESP_OK;
    }

    fclose(segment_file);
    segment_file = NULL;

    return manifest_record_segment(&manifest, manifest_path, bytes_in_segment);
}

esp_err_t session_start(void) {
    mkdir(SESSIONS_DIR, 0755);

    char base_id[MANIFEST_SESSION_ID_LEN];
    build_session_id(base_id, sizeof(base_id));

    char session_id[MANIFEST_SESSION_ID_LEN];
    snprintf(session_id, sizeof(session_id), "%s", base_id);

    for (int suffix = 1; ; suffix++) {
        snprintf(session_dir, sizeof(session_dir), "%s/%s", SESSIONS_DIR, session_id);

        if (mkdir(session_dir, 0755) == 0) {
            break;
        }
        if (errno != EEXIST || suffix > MAX_COLLISION_SUFFIX) {
            return ESP_FAIL;
        }

        snprintf(session_id, sizeof(session_id), "%s_%02d", base_id, suffix);
    }

    snprintf(manifest_path, sizeof(manifest_path), "%s/manifest.bin", session_dir);

    esp_err_t status = manifest_create(&manifest, manifest_path,
                                       session_id, (int64_t)time(NULL));
    if (status != ESP_OK) {
        return status;
    }

    return open_segment();
}

esp_err_t session_resume(const char *existing_session_dir) {
    snprintf(session_dir, sizeof(session_dir), "%s", existing_session_dir);
    snprintf(manifest_path, sizeof(manifest_path), "%s/manifest.bin", session_dir);

    esp_err_t status = manifest_open(&manifest, manifest_path);
    if (status != ESP_OK) {
        return status;
    }

    return open_segment();
}

esp_err_t session_write_packet(const uint8_t *packet, size_t num_bytes) {
    uint16_t length = (uint16_t)num_bytes;

    if (fwrite(&length, sizeof(length), 1, segment_file) != 1) {
        return ESP_FAIL;
    }
    if (fwrite(packet, 1, num_bytes, segment_file) != num_bytes) {
        return ESP_FAIL;
    }

    bytes_in_segment += sizeof(length) + num_bytes;
    frames_in_segment++;

    if (frames_in_segment >= FRAMES_PER_SEGMENT) {
        esp_err_t status = close_segment();
        if (status != ESP_OK) {
            return status;
        }
        return open_segment();
    }

    return ESP_OK;
}

esp_err_t session_finish(void) {
    esp_err_t status = close_segment();
    if (status != ESP_OK) {
        return status;
    }

    return manifest_record_complete(&manifest, manifest_path);
}