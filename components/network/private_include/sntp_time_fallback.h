#pragma once

#include <stdbool.h>
#include <stdint.h>

#define SNTP_TIME_FALLBACK_MIN_UNIX 1767225600

bool sntp_time_fallback_estimate(int64_t last_known_unix, int64_t uptime_us, int64_t now_unix,
                                 int64_t *out_unix);
