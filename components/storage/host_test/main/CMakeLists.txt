set(storage_dir "${CMAKE_CURRENT_SOURCE_DIR}/../..")
set(components_dir "${storage_dir}/..")

idf_component_register(SRCS "test_main.c"
                            "test_manifest.c"
                            "test_segment_cursor.c"
                            "test_segment_format.c"
                            "test_session_id.c"
                            "${storage_dir}/manifest.c"
                            "${storage_dir}/segment_cursor.c"
                            "${storage_dir}/segment_format.c"
                            "${storage_dir}/session_id.c"
                       INCLUDE_DIRS "."
                                    "${storage_dir}/include"
                                    "${storage_dir}/private_include"
                                    "${components_dir}/audio/include"
                                    "${components_dir}/crypto/include"
                       PRIV_REQUIRES unity)
