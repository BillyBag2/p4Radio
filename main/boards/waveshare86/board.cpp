#include "sdkconfig.h"
#include "board.h"
#include "bsp/esp32_p4_wifi6_touch_lcd_4b.h"
#include "driver/gpio.h"
#include "esp_log.h"

#ifndef BOARD_WAVESHARE86
#error "Incorrect board configuration for Waveshare ESP32-P4-86-Panel-ETH-2RO"
#endif
#ifndef CONFIG_IDF_TARGET_ESP32P4
#error "Waveshare ESP32-P4-86-Panel-ETH-2RO requires ESP32-P4"
#endif

namespace p4radio {

void board_init()
{
    ESP_LOGI("BOARD", "Initialising Waveshare ESP32-P4-86-Panel-ETH-2RO");
    gpio_hold_dis(BSP_LCD_BACKLIGHT);
}

esp_codec_dev_handle_t board_speaker_init() { return bsp_audio_codec_speaker_init(); }
bool board_display_start() { return bsp_display_start() != nullptr; }
esp_err_t board_sdcard_mount()
{
#if CONFIG_BSP_SD_FORMAT_ON_MOUNT_FAIL
    // Refuse the BSP's destructive recovery option, including in old sdkconfigs.
    return ESP_ERR_INVALID_STATE;
#else
    return bsp_sdcard_mount();
#endif
}
esp_err_t board_sdcard_unmount() { return bsp_sdcard_unmount(); }
const char *board_sdcard_path() { return BSP_SD_MOUNT_POINT; }
bool board_display_is_round() { return false; }
bool board_display_lock(int32_t timeout_ms) { return bsp_display_lock(timeout_ms); }
void board_display_unlock() { bsp_display_unlock(); }
esp_err_t board_display_brightness_set(int percent) { return bsp_display_brightness_set(percent); }
esp_err_t board_display_backlight_off() { return bsp_display_backlight_off(); }

} // namespace p4radio
