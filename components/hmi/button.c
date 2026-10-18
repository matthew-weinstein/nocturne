#include "button.h"
#include <stdbool.h>
#include "button_fsm.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define BUTTON_PIN GPIO_NUM_4

#define TASK_STACK_BYTES 4096
#define TASK_PRIORITY    10
#define TASK_CORE        0

static const char *TAG = "button";

static const char *const event_names[] = {
    [BUTTON_EVENT_SHORT]     = "short",
    [BUTTON_EVENT_LONG]      = "long",
    [BUTTON_EVENT_VERY_LONG] = "very long",
    [BUTTON_EVENT_DOWN]      = "down",
    [BUTTON_EVENT_UP]        = "up",
};

static button_handler_t handler;

static bool read_pressed(void) {
    return gpio_get_level(BUTTON_PIN) == 0;
}

static void emit(button_event_t event) {
    if (event == BUTTON_EVENT_DOWN || event == BUTTON_EVENT_UP) {
        ESP_LOGD(TAG, "button %s", event_names[event]);
    } else {
        ESP_LOGI(TAG, "%s press", event_names[event]);
    }
    handler(event);
}

static void hmi_task(void *arg) {
    (void)arg;

    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));

    button_fsm_t fsm = {0};
    TickType_t last_wake = xTaskGetTickCount();

    for (;;) {
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(BUTTON_FSM_POLL_MS));
        esp_task_wdt_reset();

        button_event_t events[BUTTON_FSM_MAX_EVENTS];
        int num_events = button_fsm_step(&fsm, read_pressed(), events);
        for (int i = 0; i < num_events; i++) {
            emit(events[i]);
        }
    }
}

esp_err_t button_init(button_handler_t on_event) {
    if (on_event == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (handler != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    const gpio_config_t pin_cfg = {
        .pin_bit_mask = 1ULL << BUTTON_PIN,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };

    esp_err_t status = gpio_config(&pin_cfg);
    if (status != ESP_OK) {
        return status;
    }

    handler = on_event;

    BaseType_t created = xTaskCreatePinnedToCore(hmi_task, "hmi_task", TASK_STACK_BYTES,
                                                 NULL, TASK_PRIORITY, NULL, TASK_CORE);
    if (created != pdPASS) {
        handler = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
