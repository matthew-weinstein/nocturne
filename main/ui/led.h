#pragma once

#include "esp_err.h"

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
esp_err_t led_set(led_color_t color);
