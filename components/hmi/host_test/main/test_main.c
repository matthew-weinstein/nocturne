#include <stdlib.h>
#include "tests.h"
#include "unity.h"

void app_main(void) {
    UNITY_BEGIN();
    run_button_fsm_tests();
    run_led_pattern_tests();
    exit(UNITY_END());
}
