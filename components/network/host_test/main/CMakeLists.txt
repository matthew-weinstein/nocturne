set(network_dir "${CMAKE_CURRENT_SOURCE_DIR}/../..")

idf_component_register(SRCS "test_main.c"
                            "test_wifi_backoff.c"
                            "${network_dir}/wifi_backoff.c"
                       INCLUDE_DIRS "."
                                    "${network_dir}/private_include"
                       PRIV_REQUIRES unity)
