idf_component_register(SRCS "test_audio_encoder.c"
                            "test_i2s_microphone.c"
                            "test_ring_buffer.c"
                       PRIV_REQUIRES unity
                                     audio
                                     esp_timer)
