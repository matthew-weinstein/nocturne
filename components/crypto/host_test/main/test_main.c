#include <stdlib.h>
#include "psa/crypto.h"
#include "tests.h"
#include "unity.h"

void setUp(void) {
    TEST_ASSERT_EQUAL_INT(PSA_SUCCESS, psa_crypto_init());
}

void app_main(void) {
    UNITY_BEGIN();
    run_aes_gcm_tests();
    run_key_manager_tests();
    run_sha1_stream_tests();
    exit(UNITY_END());
}
