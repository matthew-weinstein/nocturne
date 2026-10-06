set(crypto_dir "${CMAKE_CURRENT_SOURCE_DIR}/../..")

idf_component_register(SRCS "test_main.c"
                            "host_stubs.c"
                            "test_aes_gcm.c"
                            "test_key_manager.c"
                            "test_sha1_stream.c"
                            "${crypto_dir}/test_vectors/kat.c"
                            "${crypto_dir}/aes_gcm.c"
                            "${crypto_dir}/key_manager.c"
                            "${crypto_dir}/sha1_stream.c"
                       INCLUDE_DIRS "."
                                    "${crypto_dir}/test_vectors"
                                    "${crypto_dir}/include"
                       PRIV_REQUIRES unity
                                     mbedtls
                                     esp_hw_support
                                     esp_timer)

idf_component_get_property(mbedtls_lib mbedtls COMPONENT_LIB)
target_link_libraries(tfpsacrypto INTERFACE ${mbedtls_lib} mbedtls)
