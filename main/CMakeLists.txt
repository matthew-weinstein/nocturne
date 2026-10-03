idf_component_register(SRCS "main.c"
                            "test/self_test.c"
                       INCLUDE_DIRS "."
                                    "test"
                       REQUIRES audio
                                crypto
                                storage
                                hmi
                                system
                                esp_wifi
                                esp_netif
                                esp_event
                                nvs_flash
                                esp_https_ota
                                esp_http_client
                                app_update
                                mbedtls
                                esp_hw_support
                                esphome__micro-opus)
