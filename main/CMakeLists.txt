idf_component_register(
    SRCS
        "main.c"
        "app_state.c"
        "hardware.c"
        "counters.c"
        "storage.c"
        "audit.c"
        "environment.c"
        "console.c"
        "ble_broadcast.c"
        "ble_control.c"
        "telemetry_protocol.c"
        "web.c"
    INCLUDE_DIRS "."
    REQUIRES json mbedtls bt driver fatfs sdmmc esp_driver_sdspi esp_driver_gpio esp_driver_i2c esp_driver_ledc esp_driver_spi esp_http_server esp_wifi esp_netif esp_event esp_timer esp_hw_support nvs_flash
    EMBED_FILES "fpga.bin"
)

execute_process(COMMAND git rev-parse HEAD WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} OUTPUT_VARIABLE AUDIT_GIT OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(COMMAND git diff --quiet HEAD WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} RESULT_VARIABLE AUDIT_DIRTY)
if(AUDIT_DIRTY)
 set(AUDIT_DIRTY 1)
endif()
file(SHA256 "${CMAKE_CURRENT_SOURCE_DIR}/fpga.bin" AUDIT_FPGA)
string(TIMESTAMP AUDIT_DATE "%Y-%m-%dT%H:%M:%SZ" UTC)
configure_file(identity.h.in identity.h @ONLY)
target_include_directories(${COMPONENT_LIB} PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
