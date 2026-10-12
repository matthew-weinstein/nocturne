#include <stdlib.h>
#include "tests.h"
#include "unity.h"

void app_main(void) {
    UNITY_BEGIN();
    run_wifi_backoff_tests();
    run_sntp_time_fallback_tests();
    run_b2_auth_tests();
    exit(UNITY_END());
}
