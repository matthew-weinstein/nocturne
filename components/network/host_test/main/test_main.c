#include <stdlib.h>
#include "tests.h"
#include "unity.h"

void app_main(void) {
    UNITY_BEGIN();
    run_wifi_backoff_tests();
    exit(UNITY_END());
}
