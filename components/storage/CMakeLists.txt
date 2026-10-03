idf_component_register(SRCS "sd_card.c"
                            "manifest.c"
                            "session.c"
                       INCLUDE_DIRS "include"
                       REQUIRES audio
                                crypto
                       PRIV_REQUIRES fatfs
                                     sdmmc
                                     esp_driver_sdspi
                                     esp_driver_spi
                                     esp_driver_gpio
                                     esp_hw_support)
