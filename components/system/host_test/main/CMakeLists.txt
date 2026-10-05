set(system_dir "${CMAKE_CURRENT_SOURCE_DIR}/../..")
set(hmi_dir "${system_dir}/../hmi")

idf_component_register(SRCS "test_main.c"
                            "test_supervisor_logic.c"
                            "${system_dir}/supervisor_logic.c"
                       INCLUDE_DIRS "."
                                    "${system_dir}/include"
                                    "${system_dir}/private_include"
                                    "${hmi_dir}/include"
                       PRIV_REQUIRES unity)
