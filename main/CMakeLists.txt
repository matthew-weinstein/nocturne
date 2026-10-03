idf_component_register(SRCS "main.c" "audio/i2s_microphone.c"
                                     "audio/wav_writer.c"
                                     "audio/audio_encoder.c"
                                     "audio/ring_buffer.c"
                                     "storage/sd_card.c"
                                     "storage/session.c"
                                     "storage/manifest.c"
                                     "crypto/aes_gcm.c"
                                     "crypto/key_manager.c"
                                     "crypto/sha1_stream.c"
                                     "test/self_test.c"
                                     "hmi/led.c"
                                     "hmi/button.c"
                       INCLUDE_DIRS "."
                                    "audio"
                                    "storage"
                                    "crypto"
                                    "test"
                                    "hmi"
                       REQUIRES esp_driver_i2s
                                esp_driver_gpio
                                esp_driver_ledc
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
                                esp_timer
                                esp_hw_support
                                esphome__micro-opus)