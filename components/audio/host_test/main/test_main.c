#include <stdlib.h>
#include "ring_buffer.h"
#include "tests.h"
#include "unity.h"

void tearDown(void) {
    ring_buffer_deinit();
}

void app_main(void) {
    UNITY_BEGIN();
    run_pcm_convert_tests();
    run_ring_buffer_tests();
    exit(UNITY_END());
}
