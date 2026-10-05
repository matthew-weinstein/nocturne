set(hmi_dir "${CMAKE_CURRENT_SOURCE_DIR}/../..")

idf_component_register(SRCS "test_main.c"
                            "test_button_fsm.c"
                            "test_led_pattern.c"
                            "${hmi_dir}/button_fsm.c"
                            "${hmi_dir}/led_pattern.c"
                       INCLUDE_DIRS "."
                                    "${hmi_dir}/include"
                                    "${hmi_dir}/private_include"
                       PRIV_REQUIRES unity)
