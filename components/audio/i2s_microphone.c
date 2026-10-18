#include "i2s_microphone.h"
#include "audio_format.h"
#include "driver/i2s_std.h"
#include "esp_attr.h"
#include "freertos/FreeRTOS.h"

#define MICROPHONE_BCLK_PIN GPIO_NUM_15
#define MICROPHONE_WS_PIN   GPIO_NUM_6
#define MICROPHONE_DIN_PIN  GPIO_NUM_7

#define BYTES_PER_SAMPLE sizeof(int32_t)

#define DMA_DESCRIPTORS      8
#define DMA_FRAMES_PER_DESC  1023

static i2s_chan_handle_t microphone_channel;
static volatile uint32_t num_overflows;

static bool IRAM_ATTR count_overflow(i2s_chan_handle_t channel, i2s_event_data_t *event, void *context) {
    (void)channel;
    (void)event;
    (void)context;
    num_overflows++;
    return false;
}

esp_err_t i2s_microphone_init(void) {
    i2s_chan_config_t channel_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    channel_cfg.dma_desc_num = DMA_DESCRIPTORS;
    channel_cfg.dma_frame_num = DMA_FRAMES_PER_DESC;

    esp_err_t status = i2s_new_channel(&channel_cfg, NULL, &microphone_channel);
    if (status != ESP_OK) {
        return status;
    }

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_FORMAT_SAMPLE_RATE_HZ),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = MICROPHONE_BCLK_PIN,
            .ws   = MICROPHONE_WS_PIN,
            .dout = I2S_GPIO_UNUSED,
            .din  = MICROPHONE_DIN_PIN,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };

    std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;

    status = i2s_channel_init_std_mode(microphone_channel, &std_cfg);
    if (status != ESP_OK) {
        return status;
    }

    i2s_event_callbacks_t callbacks = {
        .on_recv_q_ovf = count_overflow,
    };
    status = i2s_channel_register_event_callback(microphone_channel, &callbacks, NULL);
    if (status != ESP_OK) {
        return status;
    }

    return i2s_channel_enable(microphone_channel);
}

esp_err_t i2s_microphone_read(int32_t *out_samples, size_t max_samples, size_t *num_samples) {
    size_t num_bytes = 0;

    esp_err_t status = i2s_channel_read(microphone_channel, out_samples, max_samples * BYTES_PER_SAMPLE,
                                        &num_bytes, portMAX_DELAY);

    *num_samples = num_bytes / BYTES_PER_SAMPLE;
    return status;
}

uint32_t i2s_microphone_overflow_count(void) {
    return num_overflows;
}