idf_component_register(SRCS "test_audio_features.c"
                            "../test_vectors/audio_features_vectors.c"
                       PRIV_INCLUDE_DIRS "../private_include"
                                         "../test_vectors"
                       PRIV_REQUIRES unity
                                     classifier
                                     esp_timer)
