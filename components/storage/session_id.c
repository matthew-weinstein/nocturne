#include "session_id.h"

#include <stdio.h>

void session_id_format(const struct tm *local, int suffix, char *out, size_t out_len) {
    char base[SESSION_ID_BASE_LEN];
    strftime(base, sizeof(base), "%Y%m%d_%H%M", local);

    if (suffix == 0) {
        snprintf(out, out_len, "%s", base);
    } else {
        snprintf(out, out_len, "%s_%02d", base, suffix);
    }
}
