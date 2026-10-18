idf_component_register(SRCS "press_mute.c" "recorder.c"
                            "backpressure.c"
                            "press_mute.c"
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "private_include"
                       PRIV_REQUIRES audio
                                     storage
                                     esp_timer)
