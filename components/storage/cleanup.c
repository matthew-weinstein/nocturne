#include "cleanup.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "manifest.h"

#define PATH_LEN          128
#define MANIFEST_FILENAME "manifest.bin"

static manifest_t manifest;

static bool is_older(const cleanup_session_t *a, const cleanup_session_t *b) {
    if (a->last_written_unix != b->last_written_unix) {
        return a->last_written_unix < b->last_written_unix;
    }
    return strcmp(a->dir, b->dir) < 0;
}

static void insert_oldest_first(cleanup_scan_t *scan, const cleanup_session_t *session) {
    int num_listed = cleanup_num_listed(scan);
    int position = num_listed;
    while (position > 0 && is_older(session, &scan->sessions[position - 1])) {
        position--;
    }

    if (position == CLEANUP_MAX_SESSIONS) {
        return;
    }

    int num_moved = (num_listed < CLEANUP_MAX_SESSIONS ? num_listed : CLEANUP_MAX_SESSIONS - 1) - position;
    memmove(&scan->sessions[position + 1], &scan->sessions[position], num_moved * sizeof(*session));
    scan->sessions[position] = *session;
}

static void check_folder(cleanup_scan_t *scan, const char *sessions_dir, const char *name) {
    if (strlen(name) >= MANIFEST_SESSION_ID_LEN) {
        return;
    }

    cleanup_session_t session = { 0 };
    if (snprintf(session.dir, sizeof(session.dir), "%s/%s", sessions_dir, name) >= (int)sizeof(session.dir)) {
        return;
    }

    char manifest_path[PATH_LEN];
    snprintf(manifest_path, sizeof(manifest_path), "%s/" MANIFEST_FILENAME, session.dir);

    struct stat info;
    if (stat(manifest_path, &info) != 0) {
        return;
    }

    if (manifest_open(&manifest, manifest_path) != ESP_OK || !manifest_fully_uploaded(&manifest)) {
        return;
    }

    session.last_written_unix = (int64_t)info.st_mtime;
    insert_oldest_first(scan, &session);
    scan->num_deletable++;
}

esp_err_t cleanup_scan(const char *sessions_dir, cleanup_scan_t *scan) {
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

int cleanup_num_listed(const cleanup_scan_t *scan) {
    return scan->num_deletable < CLEANUP_MAX_SESSIONS ? scan->num_deletable : CLEANUP_MAX_SESSIONS;
}

bool cleanup_should_delete(const cleanup_session_t *session, int64_t now_unix, uint32_t free_pct,
                           int64_t max_age_seconds) {
    if (free_pct < CLEANUP_LOW_FREE_PCT) {
        return true;
    }
    return now_unix - session->last_written_unix >= max_age_seconds;
}

bool cleanup_card_full(uint32_t free_pct) {
    return free_pct < CLEANUP_FULL_FREE_PCT;
}

static esp_err_t delete_all_but_manifest(const char *session_dir) {
    DIR *dir = opendir(session_dir);
    if (dir == NULL) {
        return ESP_FAIL;
    }

    esp_err_t status = ESP_OK;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        const char *name = entry->d_name;
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0 || strcmp(name, MANIFEST_FILENAME) == 0) {
            continue;
        }

        char path[PATH_LEN];
        if (snprintf(path, sizeof(path), "%s/%s", session_dir, name) >= (int)sizeof(path) || unlink(path) != 0) {
            status = ESP_FAIL;
        }
    }

    closedir(dir);
    return status;
}

esp_err_t cleanup_delete_session(const char *session_dir) {
    esp_err_t status = delete_all_but_manifest(session_dir);
    if (status != ESP_OK) {
        return status;
    }

    char manifest_path[PATH_LEN];
    snprintf(manifest_path, sizeof(manifest_path), "%s/" MANIFEST_FILENAME, session_dir);
    if (unlink(manifest_path) != 0 || rmdir(session_dir) != 0) {
        return ESP_FAIL;
    }
    return ESP_OK;
}
