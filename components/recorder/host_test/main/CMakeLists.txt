set(recorder_dir "${CMAKE_CURRENT_SOURCE_DIR}/../..")

idf_component_register(SRCS "test_main.c"
                            "test_backpressure.c"
                            "${recorder_dir}/backpressure.c"
                       INCLUDE_DIRS "."
                                    "${recorder_dir}/private_include"
                       PRIV_REQUIRES unity)
