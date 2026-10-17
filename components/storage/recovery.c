#include "recovery.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "manifest.h"
#include "segment_format.h"

#define PATH_LEN     128
#define FILENAME_LEN 32

static manifest_t manifest;

static bool is_newer(const recovery_session_t *a, const recovery_session_t *b) {
    if (a->last_written_unix != b->last_written_unix) {
        return a->last_written_unix > b->last_written_unix;
    }
    return strcmp(a->dir, b->dir) > 0;
}

static void insert_newest_first(recovery_scan_t *scan, const recovery_session_t *session) {
    int num_listed = recovery_num_listed(scan);
    int position = num_listed;
    while (position > 0 && is_newer(session, &scan->sessions[position - 1])) {
        position--;
    }

    if (position == RECOVERY_MAX_SESSIONS) {
        return;
    }

    int num_moved = (num_listed < RECOVERY_MAX_SESSIONS ? num_listed : RECOVERY_MAX_SESSIONS - 1) - position;
    memmove(&scan->sessions[position + 1], &scan->sessions[position], num_moved * sizeof(*session));
    scan->sessions[position] = *session;
}

static void check_folder(recovery_scan_t *scan, const char *sessions_dir, const char *name) {
    if (strlen(name) >= MANIFEST_SESSION_ID_LEN) {
        return;
    }

    recovery_session_t session = { 0 };
    if (snprintf(session.dir, sizeof(session.dir), "%s/%s", sessions_dir, name) >= (int)sizeof(session.dir)) {
        return;
    }

    char manifest_path[PATH_LEN];
    snprintf(manifest_path, sizeof(manifest_path), "%s/manifest.bin", session.dir);

    struct stat info;
    if (stat(manifest_path, &info) != 0) {
        return;
    }

    if (manifest_open(&manifest, manifest_path) != ESP_OK) {
        scan->num_unreadable++;
        return;
    }
    if (manifest.complete) {
        return;
    }

    session.last_written_unix = (int64_t)info.st_mtime;
    session.num_segments = manifest.num_segments;
    insert_newest_first(scan, &session);
    scan->num_incomplete++;
}

esp_err_t recovery_scan(const char *sessions_dir, recovery_scan_t *scan) {
    memset(scan, 0, sizeof(*scan));

    DIR *dir = opendir(sessions_dir);
    if (dir == NULL) {
        return (errno == ENOENT) ? ESP_OK : ESP_FAIL;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        check_folder(scan, sessions_dir, entry->d_name);
    }

    closedir(dir);
    return ESP_OK;
}

int recovery_num_listed(const recovery_scan_t *scan) {
    return scan->num_incomplete < RECOVERY_MAX_SESSIONS ? scan->num_incomplete : RECOVERY_MAX_SESSIONS;
}

bool recovery_should_resume(const recovery_session_t *session, int64_t now_unix, bool time_reliable,
                            int64_t window_seconds) {
    if (!time_reliable) {
        return false;
    }

    int64_t gap = now_unix - session->last_written_unix;
    return gap >= 0 && gap < window_seconds;
}

esp_err_t recovery_finish(const recovery_session_t *session, int64_t *out_ended_unix) {
    *out_ended_unix = session->last_written_unix;

    char manifest_path[PATH_LEN];
    snprintf(manifest_path, sizeof(manifest_path), "%s/manifest.bin", session->dir);

    esp_err_t err = manifest_open(&manifest, manifest_path);
    if (err != ESP_OK) {
        return err;
    }

    char filename[FILENAME_LEN];
    manifest_segment_filename(manifest.num_segments, filename, sizeof(filename));

    char segment_path[PATH_LEN];
    snprintf(segment_path, sizeof(segment_path), "%s/%s", session->dir, filename);

    struct stat info;
    if (stat(segment_path, &info) == 0) {
        *out_ended_unix = (int64_t)info.st_mtime;
        if (info.st_size > SEGMENT_FORMAT_HEADER_BYTES) {
            err = manifest_record_segment(&manifest, manifest_path, (size_t)info.st_size);
            if (err != ESP_OK) {
                return err;
            }
        }
    }

    return manifest_record_complete(&manifest, manifest_path);
}
