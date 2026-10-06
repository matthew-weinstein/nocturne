#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led.h"
#include "unity.h"

#define STEP_MS      1500
#define PULSE_CYCLES 3

typedef struct {
    const char *name;
    led_color_t color;
} named_color_t;

static const named_color_t colors[] = {
    {"green", LED_COLOR_GREEN},
    {"cyan",  LED_COLOR_CYAN},
    {"red",   LED_COLOR_RED},
    {"blue",  LED_COLOR_BLUE},
    {"white", LED_COLOR_WHITE},
    {"fault", LED_COLOR_FAULT},
};

static void hold(uint32_t ms) {
    vTaskDelay(pdMS_TO_TICKS(ms));
}

TEST_CASE("the LED shows every color and pattern (watch it)", "[hmi][manual]") {
    TEST_ASSERT_EQUAL(ESP_OK, led_init());

    size_t num_colors = sizeof(colors) / sizeof(colors[0]);
    for (size_t i = 0; i < num_colors; i++) {
        printf("solid %s\n", colors[i].name);
        TEST_ASSERT_EQUAL(ESP_OK, led_solid(colors[i].color));
        hold(STEP_MS);
    }

    printf("off\n");
    TEST_ASSERT_EQUAL(ESP_OK, led_off());
    hold(STEP_MS);

    printf("green flash, %d ms\n", LED_FLASH_MS);
    TEST_ASSERT_EQUAL(ESP_OK, led_flash(LED_COLOR_GREEN, LED_FLASH_MS));
    hold(STEP_MS);

    printf("white flash, %d ms\n", LED_RESULT_MS);
    TEST_ASSERT_EQUAL(ESP_OK, led_flash(LED_COLOR_WHITE, LED_RESULT_MS));
    hold(LED_RESULT_MS + STEP_MS);

    printf("fault pulse, %d ms on / %d ms off, %d cycles\n", LED_FAULT_ON_MS, LED_FAULT_OFF_MS, PULSE_CYCLES);
    TEST_ASSERT_EQUAL(ESP_OK, led_pulse(LED_COLOR_FAULT, LED_FAULT_ON_MS, LED_FAULT_OFF_MS));
    hold(PULSE_CYCLES * (LED_FAULT_ON_MS + LED_FAULT_OFF_MS));

    TEST_ASSERT_EQUAL(ESP_OK, led_off());
}
