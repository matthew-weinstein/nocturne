set(srcs "main.c" "system_check.c")
if(CONFIG_NOCTURNE_SYSTEM_CHECK)
    list(APPEND srcs "system_check_fault.c")
endif()

idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS "."
                       REQUIRES audio
                                crypto
                                storage
                                hmi
                                system
                                recorder
                                network
                                uploader
                                nvs_flash
                                esp_https_ota
                                esp_http_client
                                app_update
                                mbedtls
                                esp_hw_support
                                esp_wifi
                                esphome__micro-opus)

if(CONFIG_NOCTURNE_SYSTEM_CHECK)
    foreach(symbol fsync i2s_microphone_read audio_encoder_encode_frame sd_card_free_pct b2_client_upload_file)
        target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=${symbol}")
    endforeach()
endif()
