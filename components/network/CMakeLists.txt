idf_component_register(SRCS "wifi.c"
                            "wifi_backoff.c"
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "private_include"
                       PRIV_REQUIRES esp_wifi
                                     esp_netif
                                     esp_event
                                     esp_timer)
