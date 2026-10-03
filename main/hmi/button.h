#pragma once

#include "esp_err.h"

typedef enum {
    BUTTON_EVENT_SHORT,
    BUTTON_EVENT_LONG,
    BUTTON_EVENT_VERY_LONG,
} button_event_t;

typedef void (*button_handler_t)(button_event_t event);

esp_err_t button_init(button_handler_t on_event);
