#include "led.h"
#include <stdbool.h>
#include <stdint.h>
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "led_pattern.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define LED_RED_PIN   GPIO_NUM_16
#define LED_GREEN_PIN GPIO_NUM_8
#define LED_BLUE_PIN  GPIO_NUM_5

#define LED_SPEED_MODE LEDC_LOW_SPEED_MODE
#define LED_TIMER      LEDC_TIMER_0
#define LED_FREQ_HZ    5000

static const char *TAG = "led";

typedef struct {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
} led_duty_t;

static const led_duty_t brightness[LED_COLOR_COUNT] = {
    [LED_COLOR_OFF]   = {  0,  0,  0 },
    [LED_COLOR_GREEN] = {  0, 60,  0 },
    [LED_COLOR_CYAN]  = {  0, 60, 60 },
    [LED_COLOR_RED]   = { 60,  0,  0 },
    [LED_COLOR_BLUE]  = {  0,  0, 50 },
    [LED_COLOR_WHITE] = { 40, 40, 40 },
    [LED_COLOR_FAULT] = { 40,  0,  0 },
};

static const struct {
    ledc_channel_t channel;
    gpio_num_t pin;
} channels[] = {
    { LEDC_CHANNEL_0, LED_RED_PIN },
    { LEDC_CHANNEL_1, LED_GREEN_PIN },
    { LEDC_CHANNEL_2, LED_BLUE_PIN },
};

static esp_timer_handle_t pattern_timer;
static SemaphoreHandle_t pattern_lock;
static int64_t deadline_us = INT64_MAX;
static led_pattern_t pattern;
static bool pattern_lit;

static esp_err_t set_duty(ledc_channel_t channel, uint32_t duty) {
    esp_err_t status = ledc_set_duty(LED_SPEED_MODE, channel, duty);

    if (status != ESP_OK) {
        return status;
    }

    return ledc_update_duty(LED_SPEED_MODE, channel);
}

static esp_err_t show(led_color_t color) {
    const led_duty_t *duty = &brightness[color];

    esp_err_t status = set_duty(channels[0].channel, duty->red);
    if (status != ESP_OK) {
        return status;
    }

    status = set_duty(channels[1].channel, duty->green);
    if (status != ESP_OK) {
        return status;
    }

    return set_duty(channels[2].channel, duty->blue);
}

static esp_err_t schedule(uint32_t duration_ms) {
    deadline_us = esp_timer_get_time() + (int64_t)duration_ms * 1000;

    return esp_timer_start_once(pattern_timer, (uint64_t)duration_ms * 1000);
}

static void on_pattern_timer(void *arg) {
    (void)arg;
    xSemaphoreTake(pattern_lock, portMAX_DELAY);

    if (esp_timer_get_time() >= deadline_us) {
        deadline_us = INT64_MAX;
        led_pattern_step_t step = led_pattern_next(&pattern, pattern_lit);
        pattern_lit = step.lit;

        esp_err_t status = show(step.lit ? pattern.color : LED_COLOR_OFF);
        if (status == ESP_OK && step.delay_ms > 0) {
            status = schedule(step.delay_ms);
        }
        if (status != ESP_OK) {
            ESP_LOGW(TAG, "pattern step failed: %s", esp_err_to_name(status));
        }
    }

    xSemaphoreGive(pattern_lock);
}

esp_err_t led_init(void) {
    ledc_timer_config_t timer_cfg = {
        .speed_mode      = LED_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .timer_num       = LED_TIMER,
        .freq_hz         = LED_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };

    esp_err_t status = ledc_timer_config(&timer_cfg);
    if (status != ESP_OK) {
        return status;
    }

    for (size_t i = 0; i < sizeof(channels) / sizeof(channels[0]); i++) {
        ledc_channel_config_t channel_cfg = {
            .gpio_num   = channels[i].pin,
            .speed_mode = LED_SPEED_MODE,
            .channel    = channels[i].channel,
            .timer_sel  = LED_TIMER,
            .duty       = 0,
            .hpoint     = 0,
        };

        status = ledc_channel_config(&channel_cfg);
        if (status != ESP_OK) {
            return status;
        }
    }

    const esp_timer_create_args_t timer_args = {
        .callback = on_pattern_timer,
        .name     = "led_pattern",
    };

    status = esp_timer_create(&timer_args, &pattern_timer);    
    if (status != ESP_OK) {
        return status;
    }

    pattern_lock = xSemaphoreCreateMutex();
    return pattern_lock != NULL ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t led_start(const led_pattern_t *requested) {
    if (requested == NULL || !led_pattern_valid(requested)) {
        return ESP_ERR_INVALID_ARG;
    }

    if (pattern_lock == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(pattern_lock, portMAX_DELAY);

    esp_timer_stop(pattern_timer);
    deadline_us = INT64_MAX;
    pattern = *requested;

    led_pattern_step_t step = led_pattern_first(&pattern);
    pattern_lit = step.lit;

    esp_err_t status = show(pattern.color);
    if (status == ESP_OK && step.delay_ms > 0) {
        status = schedule(step.delay_ms);
    }

    xSemaphoreGive(pattern_lock);
    return status;
}

esp_err_t led_off(void) {
    return led_start(&(led_pattern_t){ LED_COLOR_OFF, 0, 0 });
}

esp_err_t led_solid(led_color_t color) {
    return led_start(&(led_pattern_t){ color, 0, 0 });
}

esp_err_t led_flash(led_color_t color, uint32_t duration_ms) {
    if (duration_ms == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    return led_start(&(led_pattern_t){ color, duration_ms, 0 });
}

esp_err_t led_pulse(led_color_t color, uint32_t on_ms, uint32_t off_ms) {
    if (on_ms == 0 || off_ms == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    return led_start(&(led_pattern_t){ color, on_ms, off_ms });
}
