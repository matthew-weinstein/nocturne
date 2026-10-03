#pragma once

#include <stddef.h>
#include "esp_err.h"

#define SHA1_STREAM_DIGEST_BYTES 20
#define SHA1_STREAM_HEX_LEN      (SHA1_STREAM_DIGEST_BYTES * 2 + 1)

/* Hashes the whole file in one read pass. out_num_bytes is the length hashed,
   which the upload body must match exactly. Not reentrant. */
esp_err_t sha1_stream_file(const char *path, char *out_hex, size_t max_hex, size_t *out_num_bytes);
