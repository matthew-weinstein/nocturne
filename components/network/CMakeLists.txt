idf_component_register(SRCS "wifi.c"
                            "wifi_backoff.c"
                            "sntp_time.c"
                            "sntp_time_fallback.c"
                            "b2_client.c"
                            "b2_auth.c"
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "private_include"
                       PRIV_REQUIRES esp_wifi
                                     esp_netif
                                     esp_event
                                     esp_timer
                                     nvs_flash
                                     esp_http_client
                                     mbedtls)
