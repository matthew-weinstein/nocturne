#include <stddef.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led.h"
#include "supervisor.h"
#include "unity.h"

#define STEP_MS 2000

typedef struct {
    supervisor_event_t event;
    supervisor_state_t expected;
} supervisor_step_t;

static const supervisor_step_t walk[] = {
    {SUPERVISOR_EVENT_RESUME_FOUND,    SUPERVISOR_STATE_RESUMING},
    {SUPERVISOR_EVENT_SHORT_PRESS,     SUPERVISOR_STATE_RESUMING},
    {SUPERVISOR_EVENT_RESUMED,         SUPERVISOR_STATE_RECORDING},
    {SUPERVISOR_EVENT_SHORT_PRESS,     SUPERVISOR_STATE_RECORDING},
    {SUPERVISOR_EVENT_VERY_LONG_PRESS, SUPERVISOR_STATE_RECORDING},
    {SUPERVISOR_EVENT_LONG_PRESS,      SUPERVISOR_STATE_FINALIZING},
    {SUPERVISOR_EVENT_SHORT_PRESS,     SUPERVISOR_STATE_FINALIZING},
    {SUPERVISOR_EVENT_FINALIZE_OK,     SUPERVISOR_STATE_IDLE},
    {SUPERVISOR_EVENT_LONG_PRESS,      SUPERVISOR_STATE_IDLE},
    {SUPERVISOR_EVENT_VERY_LONG_PRESS, SUPERVISOR_STATE_IDLE},
    {SUPERVISOR_EVENT_SHORT_PRESS,     SUPERVISOR_STATE_RECORDING},
    {SUPERVISOR_EVENT_LONG_PRESS,      SUPERVISOR_STATE_FINALIZING},
    {SUPERVISOR_EVENT_FINALIZE_FAILED, SUPERVISOR_STATE_IDLE},
    {SUPERVISOR_EVENT_FAULT,           SUPERVISOR_STATE_FAULT},
};

TEST_CASE("the live supervisor drives the LED through every transition (watch it)", "[system][manual]") {
    TEST_ASSERT_EQUAL(ESP_OK, led_init());
    TEST_ASSERT_EQUAL(ESP_OK, supervisor_init());
    TEST_ASSERT_EQUAL(SUPERVISOR_STATE_BOOT, supervisor_state());

    size_t num_steps = sizeof(walk) / sizeof(walk[0]);
    for (size_t i = 0; i < num_steps; i++) {
        TEST_ASSERT_EQUAL(ESP_OK, supervisor_post(walk[i].event));
        vTaskDelay(pdMS_TO_TICKS(STEP_MS));
        TEST_ASSERT_EQUAL_MESSAGE(walk[i].expected, supervisor_state(), "unexpected state after a step");
    }
}
