idf_component_register(SRCS "main.c" "audio/i2s_microphone.c"
                                     "audio/wav_writer.c"
                                     "audio/audio_encoder.c"
                                     "audio/ring_buffer.c"
                                     "storage/sd_card.c"
                       INCLUDE_DIRS "." "audio" "storage"
                       REQUIRES esp_driver_i2s
                                esp_driver_sdspi
                                fatfs
                                esp_wifi
                                esp_netif
                                esp_event
                                nvs_flash
                                esp_https_ota
                                esp_http_client
                                app_update
                                mbedtls
                                esphome__micro-opus)