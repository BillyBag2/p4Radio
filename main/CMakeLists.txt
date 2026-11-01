idf_component_register(SRCS "main.cpp" "radio_state.cpp" "settings.cpp" "network.cpp"
                         "streams.cpp" "stream_input.cpp" "radio_browser_dns.cpp" "station_catalog.cpp" "station_web.cpp"
                         "playback.cpp" "display.cpp" "display_skin.cpp"
                         "display_skin_square.cpp" "display_skin_circular.cpp" "display_skin_retro.cpp" "button_audio.cpp"
                    INCLUDE_DIRS "."
                    EMBED_TXTFILES "web/stations.html" "web/stations.js" "web/stations.css"
                    REQUIRES esp_wifi esp_event esp_netif lwip esp_http_server esp_http_client nvs_flash esp_timer driver mbedtls esp_codec_dev esp_audio_codec esp32_p4_wifi6_touch_lcd_4b esp_wifi_remote esp_hosted)

# LVGL 9.6 marks several legacy headers as deprecated with #warning. The
# ESP LVGL adapter 0.6.4 still includes those headers, and IDF treats the
# warnings as errors by default. Keep the warnings visible without failing
# the build until the adapter updates its includes.
idf_component_get_property(esp_lvgl_adapter_lib espressif__esp_lvgl_adapter COMPONENT_LIB)
target_compile_options(${esp_lvgl_adapter_lib} PRIVATE -Wno-error=cpp)
