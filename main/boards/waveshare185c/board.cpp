#include "sdkconfig.h"
#include "board.h"
#include <algorithm>
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_codec_dev_defaults.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_st77916.h"
#include "esp_lcd_touch_cst816s.h"
#include "esp_lvgl_port.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "driver/sdmmc_host.h"
#include "freertos/task.h"

#ifndef BOARD_WAVESHARE185C
#error "Incorrect board configuration for Waveshare ESP32-S3-Touch-LCD-1.85C V2"
#endif
#ifndef CONFIG_IDF_TARGET_ESP32S3
#error "Waveshare ESP32-S3-Touch-LCD-1.85C requires ESP32-S3"
#endif

extern "C" const st77916_lcd_init_cmd_t *waveshare185c_panel_commands(bool alternate, uint16_t *count);

namespace p4radio {
namespace {
constexpr int kResolution = 360;
constexpr int kDrawLines = 30;
i2c_master_bus_handle_t i2c_bus = nullptr;
i2c_master_dev_handle_t expander = nullptr;
sdmmc_card_t *sdcard = nullptr;

void expander_write(uint8_t reg, uint8_t value)
{
    const uint8_t bytes[] = {reg, value};
    ESP_ERROR_CHECK(i2c_master_transmit(expander, bytes, sizeof(bytes), 100));
}

void reset_panel_and_touch()
{
    // TCA9554 P0 is touch reset, P1 is LCD reset. Leave other pins as inputs.
    const uint8_t output_reg = 1;
    const uint8_t config_reg = 3;
    uint8_t output = 0, config = 0;
    ESP_ERROR_CHECK(i2c_master_transmit_receive(expander, &output_reg, 1, &output, 1, 100));
    ESP_ERROR_CHECK(i2c_master_transmit_receive(expander, &config_reg, 1, &config, 1, 100));
    expander_write(output_reg, output & ~0x03);
    expander_write(config_reg, config & ~0x03);
    vTaskDelay(pdMS_TO_TICKS(10));
    expander_write(output_reg, output | 0x03);
    vTaskDelay(pdMS_TO_TICKS(50));
}
} // namespace

void board_init()
{
    ESP_LOGI("BOARD", "Initialising Waveshare ESP32-S3-Touch-LCD-1.85C V2 / Rev2.0");
    i2c_master_bus_config_t bus{};
    bus.i2c_port = I2C_NUM_0;
    bus.sda_io_num = GPIO_NUM_11;
    bus.scl_io_num = GPIO_NUM_10;
    bus.clk_source = I2C_CLK_SRC_DEFAULT;
    bus.glitch_ignore_cnt = 7;
    bus.flags.enable_internal_pullup = true;
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus, &i2c_bus));
    i2c_device_config_t io{};
    io.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    io.device_address = 0x20;
    io.scl_speed_hz = 400000;
    ESP_ERROR_CHECK(i2c_master_bus_add_device(i2c_bus, &io, &expander));

    ledc_timer_config_t timer{};
    timer.speed_mode = LEDC_LOW_SPEED_MODE;
    timer.duty_resolution = LEDC_TIMER_10_BIT;
    timer.timer_num = LEDC_TIMER_0;
    timer.freq_hz = 5000;
    timer.clk_cfg = LEDC_AUTO_CLK;
    ESP_ERROR_CHECK(ledc_timer_config(&timer));
    ledc_channel_config_t backlight{};
    backlight.gpio_num = GPIO_NUM_5;
    backlight.speed_mode = LEDC_LOW_SPEED_MODE;
    backlight.channel = LEDC_CHANNEL_0;
    backlight.timer_sel = LEDC_TIMER_0;
    ESP_ERROR_CHECK(ledc_channel_config(&backlight));
}

esp_codec_dev_handle_t board_speaker_init()
{
    // Retain one interface for the device lifetime; speaker serialization is
    // owned by button_audio.cpp, as on the P4 boards.
    static esp_codec_dev_handle_t speaker = nullptr;
    if (speaker) return speaker;
    i2s_chan_handle_t tx = nullptr;
    i2s_chan_config_t channel = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    channel.auto_clear = true;
    ESP_ERROR_CHECK(i2s_new_channel(&channel, &tx, nullptr));
    i2s_std_config_t config{};
    config.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(24000);
    config.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
    config.gpio_cfg.mclk = GPIO_NUM_2;
    config.gpio_cfg.bclk = GPIO_NUM_48;
    config.gpio_cfg.ws = GPIO_NUM_38;
    config.gpio_cfg.dout = GPIO_NUM_47;
    config.gpio_cfg.din = GPIO_NUM_NC;
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx, &config));
    // esp_codec_dev stops the channel before its first format configuration.
    // Start it here, matching the vendor/BSP handoff, so that stop is valid.
    ESP_ERROR_CHECK(i2s_channel_enable(tx));
    audio_codec_i2s_cfg_t data_config{};
    data_config.port = I2S_NUM_0;
    data_config.tx_handle = tx;
    const audio_codec_data_if_t *data = audio_codec_new_i2s_data(&data_config);
    audio_codec_i2c_cfg_t control_config{};
    control_config.port = I2C_NUM_0;
    control_config.addr = ES8311_CODEC_DEFAULT_ADDR;
    control_config.bus_handle = i2c_bus;
    const audio_codec_ctrl_if_t *control = audio_codec_new_i2c_ctrl(&control_config);
    const audio_codec_gpio_if_t *gpio = audio_codec_new_gpio();
    if (!data || !control || !gpio) return nullptr;
    es8311_codec_cfg_t codec_config{};
    codec_config.ctrl_if = control;
    codec_config.gpio_if = gpio;
    codec_config.codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC;
    codec_config.pa_pin = GPIO_NUM_15;
    codec_config.use_mclk = true;
    const audio_codec_if_t *codec = es8311_codec_new(&codec_config);
    if (!codec) return nullptr;
    esp_codec_dev_cfg_t device{};
    device.dev_type = ESP_CODEC_DEV_TYPE_OUT;
    device.codec_if = codec;
    device.data_if = data;
    speaker = esp_codec_dev_new(&device);
    return speaker;
}

bool board_display_start()
{
    reset_panel_and_touch();
    spi_bus_config_t bus{};
    bus.sclk_io_num = GPIO_NUM_40;
    bus.data0_io_num = GPIO_NUM_46;
    bus.data1_io_num = GPIO_NUM_45;
    bus.data2_io_num = GPIO_NUM_42;
    bus.data3_io_num = GPIO_NUM_41;
    bus.data4_io_num = bus.data5_io_num = bus.data6_io_num = bus.data7_io_num = -1;
    bus.max_transfer_sz = kResolution * kDrawLines * 2;
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO));
    esp_lcd_panel_io_spi_config_t io_config{};
    io_config.cs_gpio_num = GPIO_NUM_21;
    io_config.dc_gpio_num = GPIO_NUM_NC;
    io_config.trans_queue_depth = 10;
    io_config.lcd_cmd_bits = 32;
    io_config.lcd_param_bits = 8;
    io_config.flags.quad_mode = true;
    io_config.pclk_hz = 3 * 1000 * 1000;
    esp_lcd_panel_io_handle_t io = nullptr;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI2_HOST, &io_config, &io));
    // Waveshare ships two panel revisions. Probe at the vendor's read clock,
    // then use its alternate register sequence for the 00/02/7f/7f panel.
    uint8_t id[4]{};
    const esp_err_t probe = esp_lcd_panel_io_rx_param(io, (0x0bU << 24) | (0x04U << 8), id, sizeof(id));
    if (probe != ESP_OK) ESP_LOGW("BOARD", "Panel ID unavailable; using vendor default initialization");
    ESP_ERROR_CHECK(esp_lcd_panel_io_del(io));
    io_config.pclk_hz = 80 * 1000 * 1000;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI2_HOST, &io_config, &io));
    st77916_vendor_config_t vendor{};
    vendor.flags.use_qspi_interface = true;
    const bool alternate = probe == ESP_OK && id[0] == 0 && id[1] == 2 && id[2] == 0x7f && id[3] == 0x7f;
    vendor.init_cmds = waveshare185c_panel_commands(alternate, &vendor.init_cmds_size);
    esp_lcd_panel_dev_config_t panel_config{};
    panel_config.reset_gpio_num = GPIO_NUM_NC;
    panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
    panel_config.bits_per_pixel = 16;
    panel_config.vendor_config = &vendor;
    esp_lcd_panel_handle_t panel = nullptr;
    ESP_ERROR_CHECK(esp_lcd_new_panel_st77916(io, &panel_config, &panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel, true));

    esp_lcd_panel_io_i2c_config_t touch_io_config{};
    touch_io_config.dev_addr = ESP_LCD_TOUCH_IO_I2C_CST816S_ADDRESS;
    touch_io_config.scl_speed_hz = 400000;
    touch_io_config.control_phase_bytes = 1;
    touch_io_config.lcd_cmd_bits = 8;
    touch_io_config.flags.disable_control_phase = true;
    esp_lcd_panel_io_handle_t touch_io = nullptr;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(i2c_bus, &touch_io_config, &touch_io));
    esp_lcd_touch_config_t touch_config{};
    touch_config.x_max = kResolution;
    touch_config.y_max = kResolution;
    touch_config.rst_gpio_num = GPIO_NUM_NC;
    // Poll continuously so OFF does not depend on an interrupt edge to wake.
    touch_config.int_gpio_num = GPIO_NUM_NC;
    esp_lcd_touch_handle_t touch = nullptr;
    ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_cst816s(touch_io, &touch_config, &touch));
    const uint8_t disable_auto_sleep = 1;
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(touch_io, 0xfe, &disable_auto_sleep, 1));

    lvgl_port_cfg_t port = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_ERROR_CHECK(lvgl_port_init(&port));
    lvgl_port_display_cfg_t display_config{};
    display_config.io_handle = io;
    display_config.panel_handle = panel;
    display_config.buffer_size = kResolution * kDrawLines;
    display_config.double_buffer = true;
    display_config.hres = kResolution;
    display_config.vres = kResolution;
    display_config.color_format = LV_COLOR_FORMAT_RGB565;
    display_config.flags.buff_dma = true;
    display_config.flags.swap_bytes = true;
    lv_display_t *display = lvgl_port_add_disp(&display_config);
    if (!display) return false;
    lvgl_port_touch_cfg_t input{};
    input.disp = display;
    input.handle = touch;
    return lvgl_port_add_touch(&input) != nullptr;
}

bool board_display_lock(int32_t timeout_ms) { return lvgl_port_lock(timeout_ms < 0 ? 0 : timeout_ms); }
void board_display_unlock() { lvgl_port_unlock(); }
esp_err_t board_display_brightness_set(int percent)
{
    esp_err_t result = ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0,
                                   std::clamp(percent, 0, 100) * 1023 / 100);
    return result == ESP_OK ? ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0) : result;
}
esp_err_t board_display_backlight_off() { return board_display_brightness_set(0); }
bool board_display_is_round() { return true; }
bool board_speaker_is_mono() { return false; }

const char *board_sdcard_path() { return "/sdcard"; }
esp_err_t board_sdcard_mount()
{
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    // Waveshare V2 vendor SD_MMC wiring: one-bit SDMMC, separate from QSPI LCD.
    slot.width = 1;
    slot.clk = GPIO_NUM_14;
    slot.cmd = GPIO_NUM_17;
    slot.d0 = GPIO_NUM_16;
    slot.d1 = slot.d2 = slot.d3 = GPIO_NUM_NC;
    slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
    esp_vfs_fat_sdmmc_mount_config_t config{};
    config.format_if_mount_failed = false;
    config.max_files = 2;
    return esp_vfs_fat_sdmmc_mount(board_sdcard_path(), &host, &slot, &config, &sdcard);
}
esp_err_t board_sdcard_unmount()
{
    const esp_err_t result = esp_vfs_fat_sdcard_unmount(board_sdcard_path(), sdcard);
    sdcard = nullptr;
    return result;
}

} // namespace p4radio
