idf_component_register(SRCS "test_manifest.c"
                            "test_sd_card.c"
                            "test_session.c"
                       PRIV_REQUIRES unity
                                     storage
                                     crypto
                                     audio)
