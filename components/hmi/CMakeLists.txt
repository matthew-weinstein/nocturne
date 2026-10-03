idf_component_register(SRCS "led.c"
                            "button.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES esp_driver_ledc
                                     esp_driver_gpio
                                     esp_timer)
