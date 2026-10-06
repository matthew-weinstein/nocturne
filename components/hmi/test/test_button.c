#include <stdbool.h>
#include <stdio.h>
#include "button.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "unity.h"

#define PRESS_WINDOW_MS 60000
#define POLL_MS         100

static volatile bool seen_short;
static volatile bool seen_long;
static volatile bool seen_very_long;

static void on_event(button_event_t event) {
    switch (event) {
    case BUTTON_EVENT_SHORT:
        printf("short\n");
        seen_short = true;
        break;
    case BUTTON_EVENT_LONG:
        printf("long\n");
        seen_long = true;
        break;
    case BUTTON_EVENT_VERY_LONG:
        printf("very long\n");
        seen_very_long = true;
        break;
    }
}

static bool all_seen(void) {
    return seen_short && seen_long && seen_very_long;
}

TEST_CASE("the button reports short, long and very long presses (press it)", "[hmi][manual]") {
    printf("within %d s: a short press, a 2 s hold, and a 10 s hold\n", PRESS_WINDOW_MS / 1000);
    TEST_ASSERT_EQUAL(ESP_OK, button_init(on_event));

    for (int waited_ms = 0; waited_ms < PRESS_WINDOW_MS && !all_seen(); waited_ms += POLL_MS) {
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
    }

    TEST_ASSERT_TRUE_MESSAGE(seen_short, "no short press");
    TEST_ASSERT_TRUE_MESSAGE(seen_long, "no long press");
    TEST_ASSERT_TRUE_MESSAGE(seen_very_long, "no very long press");
}
