include(${CMAKE_CURRENT_LIST_DIR}/../cmake/boards.cmake)
set(BOARD_SOURCE "boards/${MY_BOARD}/board.cpp")
set(BOARD_EXTRA_SOURCES)
set(DISPLAY_SOURCES)
set(DISPLAY_ASSETS)
set(ADVANCED_TAB "")
if(NOT "$ENV{P4RADIO_HEADLESS}" STREQUAL "ON")
    list(APPEND DISPLAY_SOURCES "display.cpp" "display_skin.cpp" "display_skin_square.cpp"
                               "display_skin_circular.cpp" "display_skin_retro.cpp" "advanced.cpp")
    list(APPEND DISPLAY_ASSETS "web/advanced.html" "web/advanced.js")
    list(APPEND BOARD_REQUIRES driver fatfs sdmmc lvgl ${BOARD_DISPLAY_REQUIRES_${MY_BOARD}})
    set(ADVANCED_TAB [[<a href="/advanced">Advanced</a>]])
endif()
if(MY_BOARD STREQUAL "waveshare185c")
    list(APPEND BOARD_EXTRA_SOURCES "boards/waveshare185c/panel_init.c")
elseif(MY_BOARD STREQUAL "esp32_4848s040")
    list(APPEND BOARD_EXTRA_SOURCES "boards/esp32_4848s040/panel_init.c"
                                    "boards/esp32_4848s040/speaker.cpp")
endif()
if(NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/${BOARD_SOURCE}")
    message(FATAL_ERROR "Missing implementation for board '${MY_BOARD}'")
endif()

set(stations_page "${CMAKE_CURRENT_BINARY_DIR}/stations.html")
idf_component_register(SRCS "${BOARD_SOURCE}" ${BOARD_EXTRA_SOURCES} ${DISPLAY_SOURCES} "main.cpp" "radio_state.cpp" "settings.cpp" "network.cpp"
                         "streams.cpp" "stream_input.cpp" "radio_browser_dns.cpp" "station_catalog.cpp" "station_web.cpp"
                         "playback.cpp" "button_audio.cpp"
                    INCLUDE_DIRS "."
                    EMBED_TXTFILES "${stations_page}" "web/stations.js" "web/stations.css" "web/navigation.css" ${DISPLAY_ASSETS}
                    REQUIRES esp_wifi esp_event esp_netif lwip esp_http_server esp_http_client nvs_flash esp_timer mbedtls esp_codec_dev esp_audio_codec ${BOARD_REQUIRES})
configure_file("web/stations.html" "${stations_page}" @ONLY)

target_compile_definitions(${COMPONENT_LIB} PRIVATE ${MY_BOARD_DEFINE}=1 P4RADIO_WIFI_HOSTED=${BOARD_WIFI_HOSTED}
                           P4RADIO_HEADLESS=$<BOOL:$ENV{P4RADIO_HEADLESS}>)

# LVGL 9.6 marks several legacy headers as deprecated with #warning. The
# ESP LVGL adapter 0.6.4 still includes those headers, and IDF treats the
# warnings as errors by default. Keep the warnings visible without failing
# the build until the adapter updates its includes.
if(MY_BOARD STREQUAL "waveshare86")
    idf_component_get_property(esp_lvgl_adapter_lib espressif__esp_lvgl_adapter COMPONENT_LIB)
    target_compile_options(${esp_lvgl_adapter_lib} PRIVATE -Wno-error=cpp)
endif()
