idf_component_register(SRCS "led.c"
                            "led_pattern.c"
                            "button.c"
                            "button_fsm.c"
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "private_include"
                       PRIV_REQUIRES esp_driver_ledc
                                     esp_driver_gpio
                                     esp_timer)
