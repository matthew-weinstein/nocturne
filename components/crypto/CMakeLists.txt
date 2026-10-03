idf_component_register(SRCS "aes_gcm.c"
                            "key_manager.c"
                            "sha1_stream.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES mbedtls
                                     esp_hw_support
                                     esp_timer)
