#include <stdlib.h>
#include "tests.h"
#include "unity.h"

void app_main(void) {
    UNITY_BEGIN();
    run_audio_features_tests();
    exit(UNITY_END());
}
