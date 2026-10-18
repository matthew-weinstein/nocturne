#include <stdlib.h>
#include "tests.h"
#include "unity.h"

void app_main(void) {
    UNITY_BEGIN();
    run_backpressure_tests();
    run_press_mute_tests();
    exit(UNITY_END());
}
