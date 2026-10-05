idf_component_register(SRCS "i2s_microphone.c"
                            "ring_buffer.c"
                            "audio_encoder.c"
                            "pcm_convert.c"
                            "wav_writer.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES esp_driver_i2s
                                     esp_driver_gpio
                                     esphome__micro-opus)
