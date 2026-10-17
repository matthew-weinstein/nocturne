idf_component_register(SRCS "recorder.c"
                            "backpressure.c"
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "private_include"
                       PRIV_REQUIRES audio
                                     storage
                                     esp_timer)
