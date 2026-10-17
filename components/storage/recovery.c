#include "recovery.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "manifest.h"

#define PATH_LEN 128

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
