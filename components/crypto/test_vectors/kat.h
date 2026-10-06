#pragma once

#include <stdint.h>
#include "aes_gcm.h"
#include "key_manager.h"

#define KAT_PASSPHRASE     "nocturne-test-passphrase"
#define KAT_SEGMENT_INDEX  42
#define KAT_CHUNK_INDEX    7
#define KAT_PC_PLAIN_BYTES 24
#define KAT_PC_RECORD_BYTES (KAT_PC_PLAIN_BYTES + AES_GCM_RECORD_OVERHEAD_BYTES)

#define KAT_SHA1_PATTERN_BYTES 10000
#define KAT_SHA1_PATTERN_HEX   "504bab9f255da75e2c3c08dfbc11061a05996bbf"
#define KAT_SHA1_EMPTY_HEX     "da39a3ee5e6b4b0d3255bfef95601890afd80709"

extern const uint8_t kat_salt[KEY_MANAGER_SALT_BYTES];
extern const uint8_t kat_key[KEY_MANAGER_KEY_BYTES];
extern const uint8_t kat_pc_record[KAT_PC_RECORD_BYTES];
