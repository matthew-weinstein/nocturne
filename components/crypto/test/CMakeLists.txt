idf_component_register(SRCS "test_aes_gcm.c"
                            "test_key_manager.c"
                            "test_sha1_stream.c"
                            "../test_vectors/kat.c"
                       PRIV_INCLUDE_DIRS "../test_vectors"
                       PRIV_REQUIRES unity
                                     crypto
                                     storage
                                     esp_hw_support)
