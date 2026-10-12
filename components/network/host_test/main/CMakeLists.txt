set(network_dir "${CMAKE_CURRENT_SOURCE_DIR}/../..")

idf_component_register(SRCS "test_main.c"
                            "test_wifi_backoff.c"
                            "test_sntp_time_fallback.c"
                            "test_b2_auth.c"
                            "${network_dir}/wifi_backoff.c"
                            "${network_dir}/sntp_time_fallback.c"
                            "${network_dir}/b2_auth.c"
                       INCLUDE_DIRS "."
                                    "${network_dir}/private_include"
                       PRIV_REQUIRES unity)
