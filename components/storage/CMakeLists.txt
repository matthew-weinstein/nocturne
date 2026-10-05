idf_component_register(SRCS "sd_card.c"
                            "manifest.c"
                            "segment_cursor.c"
                            "segment_format.c"
                            "session.c"
                            "session_id.c"
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "private_include"
                       REQUIRES audio
                                crypto
                       PRIV_REQUIRES fatfs
                                     sdmmc
                                     esp_driver_sdspi
                                     esp_driver_spi
                                     esp_driver_gpio
                                     esp_hw_support)
