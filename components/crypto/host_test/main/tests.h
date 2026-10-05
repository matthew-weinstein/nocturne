#pragma once

#include <stdint.h>

#define KAT_KEY_BYTES 32

extern const uint8_t kat_key[KAT_KEY_BYTES];

void run_aes_gcm_tests(void);
void run_key_manager_tests(void);
void run_sha1_stream_tests(void);
