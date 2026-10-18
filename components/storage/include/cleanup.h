#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#define CLEANUP_MAX_SESSIONS  8
#define CLEANUP_DIR_LEN       64
#define CLEANUP_LOW_FREE_PCT  20
#define CLEANUP_FULL_FREE_PCT 5

typedef struct {
    char dir[CLEANUP_DIR_LEN];
    int64_t last_written_unix;
} cleanup_session_t;

typedef struct {
    int num_deletable;
    cleanup_session_t sessions[CLEANUP_MAX_SESSIONS];
} cleanup_scan_t;

esp_err_t cleanup_scan(const char *sessions_dir, cleanup_scan_t *scan);
int cleanup_num_listed(const cleanup_scan_t *scan);
bool cleanup_should_delete(const cleanup_session_t *session, int64_t now_unix, uint32_t free_pct,
                           int64_t max_age_seconds);
bool cleanup_card_full(uint32_t free_pct);
esp_err_t cleanup_delete_session(const char *session_dir);
