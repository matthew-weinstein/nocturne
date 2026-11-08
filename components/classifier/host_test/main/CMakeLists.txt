set(classifier_dir "${CMAKE_CURRENT_SOURCE_DIR}/../..")

idf_component_register(SRCS "test_main.c"
                            "test_audio_features.c"
                            "${classifier_dir}/audio_features.c"
                            "${classifier_dir}/test_vectors/audio_features_vectors.c"
                       INCLUDE_DIRS "."
                                    "${classifier_dir}/private_include"
                                    "${classifier_dir}/test_vectors"
                       PRIV_REQUIRES unity)

target_link_libraries(${COMPONENT_LIB} PRIVATE m)
