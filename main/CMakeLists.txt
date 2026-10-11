file(GLOB_RECURSE app_srcs "app/*.cpp")
file(GLOB_RECURSE port_srcs "port/*.cpp" "port/*.c")

idf_component_register(
    SRCS "main.cpp" ${app_srcs} ${port_srcs}
    INCLUDE_DIRS "." "port"
    REQUIRES esp_wifi esp_netif esp_event nvs_flash fatfs sdmmc esp_driver_sdspi
             esp_driver_gpio esp_timer esp_psram)

# The reference app passes uint32_t/int32_t to %d in many printf calls; that is
# harmless on the ESP32 and was accepted by the Arduino build.
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-format)

# See __wrap_sdmmc_send_cmd_crc_on_off in port/board.cpp
target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=sdmmc_send_cmd_crc_on_off")
