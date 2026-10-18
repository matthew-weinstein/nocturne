set(recorder_dir "${CMAKE_CURRENT_SOURCE_DIR}/../..")
set(components_dir "${recorder_dir}/..")

idf_component_register(SRCS "test_press_mute.c" "test_main.c"
                            "test_backpressure.c"
                            "test_press_mute.c"
                            "${recorder_dir}/backpressure.c"
                            "${recorder_dir}/press_mute.c"
                       INCLUDE_DIRS "."
                                    "${recorder_dir}/private_include"
                                    "${components_dir}/audio/include"
                       PRIV_REQUIRES unity)
