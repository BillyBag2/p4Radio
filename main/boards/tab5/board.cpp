#include "sdkconfig.h"
#include "board.h"
#include "bsp/m5stack_tab5.h"
#include "esp_log.h"
#include "sd_pwr_ctrl_by_on_chip_ldo.h"

#ifndef BOARD_TAB5
#error "Incorrect board configuration for M5Stack TAB5"
#endif
#ifndef CONFIG_IDF_TARGET_ESP32P4
#error "M5Stack TAB5 requires ESP32-P4"
#endif
#if !CONFIG_ESP_HOSTED_SDIO_RESET_ACTIVE_HIGH
#error "TAB5 requires CONFIG_ESP_HOSTED_SDIO_RESET_ACTIVE_HIGH=y; update the existing sdkconfig in menuconfig"
#endif

namespace p4radio {
namespace {
sdmmc_card_t *sdcard = nullptr;
sd_pwr_ctrl_handle_t sd_power = nullptr;

esp_err_t release_sd_power()
{
    if (!sd_power) return ESP_OK;
    const esp_err_t result = sd_pwr_ctrl_del_on_chip_ldo(sd_power);
    if (result == ESP_OK) sd_power = nullptr;
    return result;
}
} // namespace

void board_init()
{
    ESP_LOGI("BOARD", "Initialising M5Stack TAB5");
    // RF_PTH_L_INT_H_EXT is E1.P0: select the built-in antenna before powering
    // the C6. WLAN_PWR_EN itself is E2.P0 and is handled by the BSP feature API.
    esp_io_expander_handle_t expander = bsp_io_expander_init();
    ESP_ERROR_CHECK(expander ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(esp_io_expander_set_level(expander, IO_EXPANDER_PIN_NUM_0, 0));
    ESP_ERROR_CHECK(esp_io_expander_set_output_mode(expander, IO_EXPANDER_PIN_NUM_0,
                                                 IO_EXPANDER_OUTPUT_MODE_PUSH_PULL));
    ESP_ERROR_CHECK(esp_io_expander_set_dir(expander, IO_EXPANDER_PIN_NUM_0, IO_EXPANDER_OUTPUT));
    ESP_ERROR_CHECK(bsp_feature_enable(BSP_FEATURE_WIFI, true));
}

esp_codec_dev_handle_t board_speaker_init() { return bsp_audio_codec_speaker_init(); }
esp_err_t board_sdcard_mount()
{
    if (sdcard) return ESP_ERR_INVALID_STATE;
    esp_err_t result = release_sd_power();
    if (result != ESP_OK) return result;
    // BSP 1.3.2 leaves its LDO handle allocated on mount failure. Own that
    // lifecycle here, using the BSP's host and pin configuration helpers.
    sdmmc_host_t host{};
    sdmmc_slot_config_t slot{};
    bsp_sdcard_get_sdmmc_host(SDMMC_HOST_SLOT_0, &host);
    bsp_sdcard_sdmmc_get_slot(SDMMC_HOST_SLOT_0, &slot);
    sd_pwr_ctrl_ldo_config_t power{};
    power.ldo_chan_id = 4;
    result = sd_pwr_ctrl_new_on_chip_ldo(&power, &sd_power);
    if (result != ESP_OK) return result;
    host.pwr_ctrl_handle = sd_power;
    esp_vfs_fat_sdmmc_mount_config_t config{};
    config.format_if_mount_failed = false;
    config.max_files = 2;
    result = esp_vfs_fat_sdmmc_mount(BSP_SD_MOUNT_POINT, &host, &slot, &config, &sdcard);
    if (result != ESP_OK) {
        sdcard = nullptr;
        const esp_err_t cleanup = release_sd_power();
        if (cleanup != ESP_OK) ESP_LOGW("BOARD", "SD power cleanup: %s", esp_err_to_name(cleanup));
    }
    return result;
}
esp_err_t board_sdcard_unmount()
{
    // Flush/unmount before releasing card power. Slot 1 (C6) stays active.
    const esp_err_t result = esp_vfs_fat_sdcard_unmount(BSP_SD_MOUNT_POINT, sdcard);
    sdcard = nullptr;
    const esp_err_t power = release_sd_power();
    return result == ESP_OK ? power : result;
}
const char *board_sdcard_path() { return BSP_SD_MOUNT_POINT; }
bool board_display_start()
{
    lv_display_t *display = bsp_display_start();
    if (!display) return false;
    if (!bsp_display_lock(0)) return false;
    // The panel's native orientation is portrait. Rotate through the LVGL port
    // so its touch mapping follows the 1280 x 720 landscape display as well.
    bsp_display_rotate(display, LV_DISPLAY_ROTATION_90);
    bsp_display_unlock();
    return true;
}
// The LVGL port uses zero for an indefinite wait; callers use -1.
bool board_display_lock(int32_t timeout_ms) { return bsp_display_lock(timeout_ms < 0 ? 0 : timeout_ms); }
void board_display_unlock() { bsp_display_unlock(); }
esp_err_t board_display_brightness_set(int percent) { return bsp_display_brightness_set(percent); }
esp_err_t board_display_backlight_off() { return bsp_display_backlight_off(); }
bool board_display_is_round() { return false; }
bool board_speaker_is_mono() { return false; }

} // namespace p4radio
