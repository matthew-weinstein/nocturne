#include "sntp_time_fallback.h"

bool sntp_time_fallback_estimate(int64_t last_known_unix, int64_t uptime_us, int64_t now_unix,
                                 int64_t *out_unix) {
    if (last_known_unix < SNTP_TIME_FALLBACK_MIN_UNIX || uptime_us < 0) {
        return false;
    }

    int64_t estimate = last_known_unix + uptime_us / 1000000;
    if (estimate <= now_unix) {
        return false;
    }

    *out_unix = estimate;
    return true;
}
