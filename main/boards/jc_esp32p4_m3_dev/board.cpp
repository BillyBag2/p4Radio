#include "sdkconfig.h"
#include "board.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "esp_codec_dev_defaults.h"
#include "esp_log.h"
#if !P4RADIO_HEADLESS
#include <algorithm>
#include "driver/sdmmc_host.h"
#include "esp_check.h"
#include "esp_lcd_jd9365_10_1.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_ldo_regulator.h"
#include "esp_lvgl_port.h"
#include "esp_vfs_fat.h"
#include "sd_pwr_ctrl_by_on_chip_ldo.h"
#endif

#if !defined(BOARD_JC_ESP32P4_M3_DEV) || !defined(CONFIG_IDF_TARGET_ESP32P4)
#error "JC-ESP32P4-M3-DEV requires its board configuration and the ESP32-P4 target"
#endif

#if !CONFIG_ESP_HOSTED_HOST_TRANSPORT_BUS_SDIO || CONFIG_ESP_HOSTED_HOST_RESET_GPIO != 54
#error "JC-ESP32P4-M3-DEV requires ESP-Hosted SDIO with C6 CHIP_PU on GPIO54; update its sdkconfig"
#endif

namespace p4radio {
namespace {

constexpr char kTag[] = "BOARD";
// The Waveshare JD9365 driver uses this same bus for its panel power-on sequence.
constexpr i2c_port_t kI2cPort = I2C_NUM_1;
i2c_master_bus_handle_t i2c_bus = nullptr;

#if !P4RADIO_HEADLESS
constexpr int kPanelWidth = 800;
constexpr int kPanelHeight = 1280;
constexpr uint8_t kBacklightAddress = 0x45;
i2c_master_dev_handle_t backlight = nullptr;
bool display_ready = false;
sdmmc_card_t *sdcard = nullptr;
sd_pwr_ctrl_handle_t sd_power = nullptr;

esp_err_t start_panel(esp_ldo_channel_handle_t &phy, esp_lcd_dsi_bus_handle_t &dsi,
                      esp_lcd_panel_io_handle_t &io, esp_lcd_panel_handle_t &panel)
{
    esp_ldo_channel_config_t power{};
    power.chan_id = 3;
    power.voltage_mv = 2500;
    ESP_RETURN_ON_ERROR(esp_ldo_acquire_channel(&power, &phy), kTag, "DSI PHY power");

    esp_lcd_dsi_bus_config_t bus{};
    bus.num_data_lanes = 2;
    bus.lane_bit_rate_mbps = 1500;
    ESP_RETURN_ON_ERROR(esp_lcd_new_dsi_bus(&bus, &dsi), kTag, "DSI bus");
    const esp_lcd_dbi_io_config_t dbi = JD9365_PANEL_IO_DBI_CONFIG();
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_dbi(dsi, &dbi, &io), kTag, "DSI command interface");

    // Waveshare's 10.1-inch JD9365 timing. Assign fields by name because
    // the driver's C initializer predates the ESP-IDF 6 DPI field order.
    esp_lcd_dpi_panel_config_t dpi{};
    dpi.dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT;
    dpi.dpi_clock_freq_mhz = 80;
    dpi.in_color_format = LCD_COLOR_FMT_RGB565;
    dpi.video_timing.h_size = kPanelWidth;
    dpi.video_timing.v_size = kPanelHeight;
    dpi.video_timing.hsync_back_porch = 20;
    dpi.video_timing.hsync_pulse_width = 20;
    dpi.video_timing.hsync_front_porch = 40;
    dpi.video_timing.vsync_back_porch = 10;
    dpi.video_timing.vsync_pulse_width = 4;
    dpi.video_timing.vsync_front_porch = 30;
    jd9365_vendor_config_t vendor{};
    vendor.mipi_config.dsi_bus = dsi;
    vendor.mipi_config.dpi_config = &dpi;
    vendor.mipi_config.lane_num = bus.num_data_lanes;
    vendor.flags.use_mipi_interface = true;
    esp_lcd_panel_dev_config_t config{};
    config.reset_gpio_num = GPIO_NUM_NC;
    config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
    config.bits_per_pixel = 16;
    config.vendor_config = &vendor;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_jd9365(io, &config, &panel), kTag, "JD9365 panel");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(panel), kTag, "JD9365 reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(panel), kTag, "JD9365 initialization");
    return esp_lcd_panel_disp_on_off(panel, true);
}

esp_err_t add_touch(lv_display_t *display)
{
    esp_lcd_panel_io_i2c_config_t io_config{};
    io_config.dev_addr = ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS;
    if (i2c_master_probe(i2c_bus, io_config.dev_addr, 50) != ESP_OK) {
        io_config.dev_addr = ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS_BACKUP;
        const esp_err_t result = i2c_master_probe(i2c_bus, io_config.dev_addr, 50);
        if (result != ESP_OK) return result;
    }
    io_config.scl_speed_hz = 400000;
    io_config.control_phase_bytes = 1;
    io_config.lcd_cmd_bits = 16;
    io_config.flags.disable_control_phase = true;
    esp_lcd_panel_io_handle_t io = nullptr;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(i2c_bus, &io_config, &io), kTag, "GT911 I2C");
    esp_lcd_touch_config_t config{};
    config.x_max = kPanelWidth;
    config.y_max = kPanelHeight;
    config.rst_gpio_num = GPIO_NUM_NC;
    config.int_gpio_num = GPIO_NUM_NC;
    esp_lcd_touch_handle_t touch = nullptr;
    esp_err_t result = esp_lcd_touch_new_i2c_gt911(io, &config, &touch);
    if (result == ESP_OK) {
        lvgl_port_touch_cfg_t input{};
        input.disp = display;
        input.handle = touch;
        if (lvgl_port_add_touch(&input)) return ESP_OK;
        esp_lcd_touch_del(touch);
        result = ESP_ERR_NO_MEM;
    }
    esp_lcd_panel_io_del(io);
    return result;
}

esp_err_t release_sd_power()
{
    if (!sd_power) return ESP_OK;
    const esp_err_t result = sd_pwr_ctrl_del_on_chip_ldo(sd_power);
    if (result == ESP_OK) sd_power = nullptr;
    return result;
}
#endif

} // namespace

void board_init()
{
    ESP_LOGI(kTag, "Initialising GUITION JC-ESP32P4-M3-DEV");
    i2c_master_bus_config_t bus{};
    bus.i2c_port = kI2cPort;
    bus.sda_io_num = GPIO_NUM_7;
    bus.scl_io_num = GPIO_NUM_8;
    bus.clk_source = I2C_CLK_SRC_DEFAULT;
    bus.glitch_ignore_cnt = 7;
    bus.flags.enable_internal_pullup = true;
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus, &i2c_bus));
}

esp_codec_dev_handle_t board_speaker_init()
{
    // Keep the codec and I2S channel for the lifetime of the radio. The caller
    // serializes speaker access and changes format at the start of each stream.
    static esp_codec_dev_handle_t speaker = nullptr;
    if (speaker) return speaker;

    i2s_chan_handle_t tx = nullptr;
    i2s_chan_config_t channel = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    channel.auto_clear = true;
    ESP_ERROR_CHECK(i2s_new_channel(&channel, &tx, nullptr));
    i2s_std_config_t config{};
    config.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(24000);
    config.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
    config.gpio_cfg.mclk = GPIO_NUM_13;
    config.gpio_cfg.bclk = GPIO_NUM_12;
    config.gpio_cfg.ws = GPIO_NUM_10;
    config.gpio_cfg.dout = GPIO_NUM_9;
    config.gpio_cfg.din = GPIO_NUM_NC;
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx, &config));
    // esp_codec_dev stops the channel before applying its first sample format.
    ESP_ERROR_CHECK(i2s_channel_enable(tx));

    audio_codec_i2s_cfg_t data_config{};
    data_config.port = I2S_NUM_0;
    data_config.tx_handle = tx;
    const audio_codec_data_if_t *data = audio_codec_new_i2s_data(&data_config);
    audio_codec_i2c_cfg_t control_config{};
    control_config.port = kI2cPort;
    control_config.addr = ES8311_CODEC_DEFAULT_ADDR;
    control_config.bus_handle = i2c_bus;
    const audio_codec_ctrl_if_t *control = audio_codec_new_i2c_ctrl(&control_config);
    const audio_codec_gpio_if_t *gpio = audio_codec_new_gpio();
    const audio_codec_if_t *codec = nullptr;
    if (data && control && gpio) {
        es8311_codec_cfg_t codec_config{};
        codec_config.ctrl_if = control;
        codec_config.gpio_if = gpio;
        codec_config.codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC;
        codec_config.pa_pin = GPIO_NUM_11;
        codec_config.use_mclk = true;
        codec = es8311_codec_new(&codec_config);
        if (codec) {
            esp_codec_dev_cfg_t device{};
            device.dev_type = ESP_CODEC_DEV_TYPE_OUT;
            device.codec_if = codec;
            device.data_if = data;
            speaker = esp_codec_dev_new(&device);
            if (speaker) return speaker;
        }
    }
    audio_codec_delete_codec_if(codec);
    audio_codec_delete_ctrl_if(control);
    audio_codec_delete_gpio_if(gpio);
    audio_codec_delete_data_if(data);
    i2s_channel_disable(tx);
    i2s_del_channel(tx);
    return nullptr;
}

bool board_speaker_is_mono() { return true; }

#if !P4RADIO_HEADLESS
bool board_display_start()
{
    // A bare carrier has no panel. Check before issuing DSI commands and
    // allow Wi-Fi setup and playback over the normal startup path.
    if (i2c_master_probe(i2c_bus, kBacklightAddress, 50) != ESP_OK) {
        ESP_LOGW(kTag, "Waveshare 10.1-DSI-TOUCH-A controller not detected");
        return false;
    }
    i2c_device_config_t backlight_config{};
    backlight_config.device_address = kBacklightAddress;
    backlight_config.scl_speed_hz = 100000;
    if (i2c_master_bus_add_device(i2c_bus, &backlight_config, &backlight) != ESP_OK) return false;

    esp_ldo_channel_handle_t phy = nullptr;
    esp_lcd_dsi_bus_handle_t dsi = nullptr;
    esp_lcd_panel_io_handle_t io = nullptr;
    esp_lcd_panel_handle_t panel = nullptr;
    esp_err_t result = start_panel(phy, dsi, io, panel);
    const lvgl_port_cfg_t port = ESP_LVGL_PORT_INIT_CONFIG();
    if (result == ESP_OK) result = lvgl_port_init(&port);
    if (result == ESP_OK) {
        lvgl_port_display_cfg_t config{};
        config.io_handle = io;
        config.panel_handle = panel;
        config.buffer_size = kPanelWidth * kPanelHeight / 10;
        config.double_buffer = true;
        config.hres = kPanelWidth;
        config.vres = kPanelHeight;
        config.color_format = LV_COLOR_FORMAT_RGB565;
        config.flags.buff_spiram = true;
        config.flags.sw_rotate = true;
        const lvgl_port_display_dsi_cfg_t dsi_config{};
        lv_display_t *display = lvgl_port_add_disp_dsi(&config, &dsi_config);
        if (display) {
            lvgl_port_lock(0);
            lv_display_set_rotation(display, LV_DISPLAY_ROTATION_270);
            lvgl_port_unlock();
            const esp_err_t touch_result = add_touch(display);
            if (touch_result != ESP_OK) ESP_LOGW(kTag, "GT911 touch unavailable: %s", esp_err_to_name(touch_result));
            display_ready = true;
            return true;
        }
        lvgl_port_deinit();
        result = ESP_ERR_NO_MEM;
    }
    ESP_LOGE(kTag, "Display startup: %s", esp_err_to_name(result));
    board_display_backlight_off();
    i2c_master_bus_rm_device(backlight);
    backlight = nullptr;
    if (panel) esp_lcd_panel_del(panel);
    if (io) esp_lcd_panel_io_del(io);
    if (dsi) esp_lcd_del_dsi_bus(dsi);
    if (phy) esp_ldo_release_channel(phy);
    return false;
}

bool board_display_is_round() { return false; }
bool board_display_lock(int32_t timeout_ms) { return display_ready && lvgl_port_lock(timeout_ms < 0 ? 0 : timeout_ms); }
void board_display_unlock() { lvgl_port_unlock(); }
esp_err_t board_display_brightness_set(int percent)
{
    if (!backlight) return ESP_ERR_INVALID_STATE;
    const uint8_t command[] = {0x96, static_cast<uint8_t>(std::clamp(percent, 0, 100) * 255 / 100)};
    return i2c_master_transmit(backlight, command, sizeof(command), 50);
}
esp_err_t board_display_backlight_off() { return board_display_brightness_set(0); }

const char *board_sdcard_path() { return "/sdcard"; }
esp_err_t board_sdcard_mount()
{
    if (sdcard) return ESP_ERR_INVALID_STATE;
    ESP_RETURN_ON_ERROR(release_sd_power(), kTag, "Release previous microSD power handle");
    sd_pwr_ctrl_ldo_config_t power{};
    power.ldo_chan_id = 4;
    ESP_RETURN_ON_ERROR(sd_pwr_ctrl_new_on_chip_ldo(&power, &sd_power), kTag, "microSD power");

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.slot = SDMMC_HOST_SLOT_0;
    host.pwr_ctrl_handle = sd_power;
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 4;
    slot.clk = GPIO_NUM_43;
    slot.cmd = GPIO_NUM_44;
    slot.d0 = GPIO_NUM_39;
    slot.d1 = GPIO_NUM_40;
    slot.d2 = GPIO_NUM_41;
    slot.d3 = GPIO_NUM_42;
    esp_vfs_fat_sdmmc_mount_config_t config{};
    config.format_if_mount_failed = false;
    config.max_files = 2;
    const esp_err_t result = esp_vfs_fat_sdmmc_mount(board_sdcard_path(), &host, &slot, &config, &sdcard);
    if (result != ESP_OK) {
        sdcard = nullptr;
        const esp_err_t cleanup = release_sd_power();
        if (cleanup != ESP_OK) ESP_LOGW(kTag, "microSD power cleanup: %s", esp_err_to_name(cleanup));
    }
    return result;
}

esp_err_t board_sdcard_unmount()
{
    if (!sdcard) return ESP_ERR_INVALID_STATE;
    const esp_err_t result = esp_vfs_fat_sdcard_unmount(board_sdcard_path(), sdcard);
    sdcard = nullptr;
    const esp_err_t power = release_sd_power();
    return result == ESP_OK ? power : result;
}
#endif

} // namespace p4radio
