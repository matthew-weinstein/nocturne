#pragma once

#include <stddef.h>
#include <time.h>

#define SESSION_ID_BASE_LEN 16

void session_id_format(const struct tm *local, int suffix, char *out, size_t out_len);
