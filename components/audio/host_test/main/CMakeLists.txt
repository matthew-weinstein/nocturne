set(audio_dir "${CMAKE_CURRENT_SOURCE_DIR}/../..")

idf_component_register(SRCS "test_main.c"
                            "test_pcm_convert.c"
                            "test_ring_buffer.c"
                            "${audio_dir}/pcm_convert.c"
                            "${audio_dir}/ring_buffer.c"
                       INCLUDE_DIRS "."
                                    "${audio_dir}/include"
                       PRIV_REQUIRES unity)
