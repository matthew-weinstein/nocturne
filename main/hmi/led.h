#pragma once

#include <stdint.h>
#include "esp_err.h"

#define LED_FLASH_MS     300
#define LED_RESULT_MS    1000
#define LED_FAULT_ON_MS  1000
#define LED_FAULT_OFF_MS 2000

typedef enum {
    LED_COLOR_OFF,
    LED_COLOR_GREEN,
    LED_COLOR_CYAN,
    LED_COLOR_RED,
    LED_COLOR_BLUE,
    LED_COLOR_WHITE,
    LED_COLOR_FAULT,
    LED_COLOR_COUNT,
} led_color_t;

esp_err_t led_init(void);
esp_err_t led_off(void);
esp_err_t led_solid(led_color_t color);
esp_err_t led_flash(led_color_t color, uint32_t duration_ms);
esp_err_t led_pulse(led_color_t color, uint32_t on_ms, uint32_t off_ms);
