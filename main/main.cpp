#include "radio.hpp"
#include "button_audio.hpp"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"

using namespace p4radio;

extern "C" void app_main(void)
{
    esp_err_t nvs_result = nvs_flash_init();
    if (nvs_result == ESP_ERR_NVS_NO_FREE_PAGES || nvs_result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_result = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs_result);
    // Invalidate older high-bitrate stream URLs once, preserving radio/Wi-Fi preferences.
    nvs_handle_t cache;
    if (nvs_open("streams96", NVS_READWRITE, &cache) == ESP_OK) {
        uint32_t revision = 0;
        if (nvs_get_u32(cache, "revision", &revision) != ESP_OK || revision != 4) {
            esp_err_t result = nvs_erase_all(cache);
            if (result == ESP_OK) result = nvs_set_u32(cache, "revision", 4);
            if (result == ESP_OK) result = nvs_commit(cache);
            ESP_LOGI(kTag, "Stream cache reset: %s", esp_err_to_name(result));
        }
        nvs_close(cache);
    }
    load_preferences();
    gpio_hold_dis(BSP_LCD_BACKLIGHT);
    initialize_audio();
    initialize_button_audio();
    build_ui();
    const bool network_ready = ensure_network_stack();
    if (!network_ready) {
        // Ethernet remains a possible fallback on the 86-panel assembly, but this build
        // intentionally reports the missing C6 link rather than pretending that P4 has Wi-Fi.
        set_status("No C6 Wi-Fi: check the coprocessor firmware or use Ethernet");
    }
    xTaskCreate(playback_task, "radio_playback", 8192, nullptr, 5, nullptr);
}
