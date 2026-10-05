#include "supervisor.h"
#include <stddef.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "led.h"
#include "supervisor_logic.h"

#define QUEUE_LENGTH 8

#define TASK_STACK_BYTES 4096
#define TASK_PRIORITY    15
#define TASK_CORE        0

static const char *TAG = "supervisor";

static const char *const state_names[] = {
    [SUPERVISOR_STATE_BOOT]       = "BOOT",
    [SUPERVISOR_STATE_IDLE]       = "IDLE",
    [SUPERVISOR_STATE_RECORDING]  = "RECORDING",
    [SUPERVISOR_STATE_FINALIZING] = "FINALIZING",
    [SUPERVISOR_STATE_RESUMING]   = "RESUMING",
    [SUPERVISOR_STATE_FAULT]      = "FAULT",
};

static const char *const event_names[] = {
    [SUPERVISOR_EVENT_BOOT_DONE]       = "boot done",
    [SUPERVISOR_EVENT_RESUME_FOUND]    = "resume found",
    [SUPERVISOR_EVENT_RESUMED]         = "resumed",
    [SUPERVISOR_EVENT_SHORT_PRESS]     = "short press",
    [SUPERVISOR_EVENT_LONG_PRESS]      = "long press",
    [SUPERVISOR_EVENT_VERY_LONG_PRESS] = "very long press",
    [SUPERVISOR_EVENT_FINALIZE_OK]     = "finalize ok",
    [SUPERVISOR_EVENT_FINALIZE_FAILED] = "finalize failed",
    [SUPERVISOR_EVENT_FAULT]           = "fault",
};

static QueueHandle_t events;
static volatile supervisor_state_t state = SUPERVISOR_STATE_BOOT;

static void handle_in_place(supervisor_state_t current, supervisor_event_t event) {
    switch (supervisor_logic_action_for(current, event)) {
    case SUPERVISOR_LOGIC_ACTION_HEALTH_CHECK:
        ESP_LOGI(TAG, "health check requested");
        break;
    case SUPERVISOR_LOGIC_ACTION_FACTORY_RESET:
        ESP_LOGW(TAG, "factory reset requested");
        break;
    case SUPERVISOR_LOGIC_ACTION_NONE:
        ESP_LOGD(TAG, "%s ignored in %s", event_names[event], state_names[current]);
        break;
    }
}

static void supervisor_task(void *arg) {
    (void)arg;

    supervisor_event_t event;

    for (;;) {
        xQueueReceive(events, &event, portMAX_DELAY);

        supervisor_state_t current = state;
        supervisor_state_t next = supervisor_logic_next_state(current, event);

        if (next == current) {
            handle_in_place(current, event);
            continue;
        }

        ESP_LOGI(TAG, "%s -> %s (%s)", state_names[current], state_names[next], event_names[event]);
        state = next;

        led_pattern_t pattern;
        if (supervisor_logic_led_for(next, event, &pattern)) {
            esp_err_t err = led_start(&pattern);

            if (err != ESP_OK) {
                ESP_LOGW(TAG, "led update for %s failed: %s", state_names[next], esp_err_to_name(err));
            }
        }
    }
}

esp_err_t supervisor_init(void) {
    if (events != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    events = xQueueCreate(QUEUE_LENGTH, sizeof(supervisor_event_t));
    if (events == NULL) {
        return ESP_ERR_NO_MEM;
    }

    BaseType_t created = xTaskCreatePinnedToCore(supervisor_task, "supervisor", TASK_STACK_BYTES,
                                                 NULL, TASK_PRIORITY, NULL, TASK_CORE);
    if (created != pdPASS) {
        vQueueDelete(events);
        events = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t supervisor_post(supervisor_event_t event) {
    if (events == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xQueueSend(events, &event, 0) != pdTRUE) {
        ESP_LOGW(TAG, "queue full, dropped %s", event_names[event]);
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

supervisor_state_t supervisor_state(void) {
    return state;
}
