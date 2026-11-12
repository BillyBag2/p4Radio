# Authoritative list of implemented boards and required ESP-IDF chip targets.
# Add hardware together with its preset, SDK defaults and implementation.
set(SUPPORTED_BOARDS waveshare86 tab5 waveshare185c esp32_4848s040)
set(BOARD_TARGET_waveshare86 esp32p4)
set(BOARD_TARGET_tab5 esp32p4)
set(BOARD_TARGET_waveshare185c esp32s3)
set(BOARD_TARGET_esp32_4848s040 esp32s3)
set(BOARD_REQUIRES_waveshare86 esp32_p4_wifi6_touch_lcd_4b esp_wifi_remote esp_hosted)
set(BOARD_REQUIRES_tab5 m5stack_tab5 esp_wifi_remote esp_hosted)
set(BOARD_REQUIRES_waveshare185c esp_lvgl_port esp_lcd_st77916 esp_lcd_touch_cst816s
    esp_driver_i2s esp_driver_i2c esp_driver_ledc esp_driver_spi esp_lcd)
set(BOARD_REQUIRES_esp32_4848s040 esp_lvgl_port esp_lcd_st7701 esp_lcd_panel_io_additions
    esp_lcd_touch_gt911 esp_driver_i2s esp_driver_i2c esp_driver_ledc esp_driver_spi esp_lcd)
set(BOARD_WIFI_HOSTED_waveshare86 1)
set(BOARD_WIFI_HOSTED_tab5 1)
set(BOARD_WIFI_HOSTED_waveshare185c 0)
set(BOARD_WIFI_HOSTED_esp32_4848s040 0)

set(MY_BOARD "$ENV{MY_BOARD}")
if(MY_BOARD STREQUAL "" AND DEFINED P4RADIO_CONFIGURED_BOARD)
    # Ninja's automatic CMake regeneration does not reapply preset environment.
    # An existing isolated cache already records the board selected by its preset.
    set(MY_BOARD "${P4RADIO_CONFIGURED_BOARD}")
endif()
if(MY_BOARD STREQUAL "")
    message(FATAL_ERROR "MY_BOARD is not set. Select an ESP-IDF project configuration or use idf.py --preset waveshare86.")
endif()
if(NOT MY_BOARD IN_LIST SUPPORTED_BOARDS)
    message(FATAL_ERROR "Unsupported board: '${MY_BOARD}'. Implemented boards: ${SUPPORTED_BOARDS}")
endif()
# Also make the selection available to dependency-manifest conditions during
# automatic regeneration. This never selects or changes the chip target.
set(ENV{MY_BOARD} "${MY_BOARD}")
string(TOUPPER "${MY_BOARD}" MY_BOARD_UPPER)
string(REGEX REPLACE "[^A-Z0-9_]" "_" MY_BOARD_UPPER "${MY_BOARD_UPPER}")
set(MY_BOARD_DEFINE "BOARD_${MY_BOARD_UPPER}")
set(BOARD_REQUIRES "${BOARD_REQUIRES_${MY_BOARD}}")
set(BOARD_WIFI_HOSTED "${BOARD_WIFI_HOSTED_${MY_BOARD}}")

function(p4radio_validate_board_target)
    set(expected_target "${BOARD_TARGET_${MY_BOARD}}")
    if(NOT IDF_TARGET STREQUAL expected_target)
        message(FATAL_ERROR "Board '${MY_BOARD}' requires '${expected_target}', but the selected chip is '${IDF_TARGET}'. Select the matching ESP-IDF chip target.")
    endif()
    message(STATUS "Board: ${MY_BOARD}; chip: ${IDF_TARGET}; define: ${MY_BOARD_DEFINE}")
endfunction()
