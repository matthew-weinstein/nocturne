#include <stdlib.h>
#include "tests.h"
#include "unity.h"

void app_main(void) {
    UNITY_BEGIN();
    run_manifest_tests();
    run_segment_cursor_tests();
    run_segment_format_tests();
    run_session_id_tests();
    exit(UNITY_END());
}
