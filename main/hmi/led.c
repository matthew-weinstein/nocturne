#include "led.h"
#include <stdint.h>
#include "driver/gpio.h"
#include "driver/ledc.h"

#define LED_RED_PIN   GPIO_NUM_5
#define LED_GREEN_PIN GPIO_NUM_16
#define LED_BLUE_PIN  GPIO_NUM_8

#define LED_SPEED_MODE LEDC_LOW_SPEED_MODE
#define LED_TIMER      LEDC_TIMER_0
#define LED_FREQ_HZ    5000

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

static esp_err_t set_duty(ledc_channel_t channel, uint32_t duty) {
    esp_err_t status = ledc_set_duty(LED_SPEED_MODE, channel, duty);
    if (status != ESP_OK) {
        return status;
    }
    return ledc_update_duty(LED_SPEED_MODE, channel);
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

    return ESP_OK;
}

esp_err_t led_set(led_color_t color) {
    if (color >= LED_COLOR_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

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
