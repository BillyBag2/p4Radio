#include "sdkconfig.h"
#include "board.h"
#include <algorithm>
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "driver/sdspi_host.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_io_additions.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_st7701.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_lvgl_port.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"

#ifndef BOARD_ESP32_4848S040
#error "Incorrect board configuration for GUITION ESP32-4848S040"
#endif
#ifndef CONFIG_IDF_TARGET_ESP32S3
#error "GUITION ESP32-4848S040 requires ESP32-S3"
#endif

extern "C" const st7701_lcd_init_cmd_t *esp32_4848s040_panel_commands(uint16_t *count);

namespace p4radio {
namespace {
constexpr int kResolution = 480;
constexpr int kBacklight = 38;
constexpr int kLcdCs = 39;
constexpr int kSharedClock = 48;
constexpr int kSharedMosi = 47;
i2c_master_bus_handle_t i2c_bus = nullptr;
sdmmc_card_t *sdcard = nullptr;
bool panel_ready = false;

esp_lcd_touch_handle_t create_touch()
{
    // Neither RESET nor INT is wired. Probe both strap-selected addresses and
    // poll continuously, including while OFF, rather than depending on an edge.
    uint8_t address = ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS;
    if (i2c_master_probe(i2c_bus, address, 100) != ESP_OK)
        address = ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS_BACKUP;
    if (i2c_master_probe(i2c_bus, address, 100) != ESP_OK) {
        ESP_LOGE("BOARD", "GT911 did not respond at either I2C address");
        return nullptr;
    }
    esp_lcd_panel_io_i2c_config_t io_config{};
    io_config.dev_addr = address;
    io_config.scl_speed_hz = 400000;
    io_config.control_phase_bytes = 1;
    io_config.lcd_cmd_bits = 16;
    io_config.flags.disable_control_phase = true;
    esp_lcd_panel_io_handle_t io = nullptr;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(i2c_bus, &io_config, &io));
    esp_lcd_touch_config_t config{};
    config.x_max = kResolution;
    config.y_max = kResolution;
    config.rst_gpio_num = GPIO_NUM_NC;
    config.int_gpio_num = GPIO_NUM_NC;
    // This board's touch orientation needs a 180-degree correction relative
    // to the vendor demo's mirrored axes. Leave both raw axes unmirrored.
    config.flags.mirror_x = false;
    config.flags.mirror_y = false;
    esp_lcd_touch_handle_t touch = nullptr;
    ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_gt911(io, &config, &touch));
    return touch;
}
} // namespace

void board_init()
{
    ESP_LOGI("BOARD", "Initialising GUITION ESP32-4848S040 speaker variant (NS4168)");
    i2c_master_bus_config_t bus{};
    bus.i2c_port = I2C_NUM_0;
    bus.sda_io_num = GPIO_NUM_19;
    bus.scl_io_num = GPIO_NUM_45;
    bus.clk_source = I2C_CLK_SRC_DEFAULT;
    bus.glitch_ignore_cnt = 7;
    bus.flags.enable_internal_pullup = true;
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus, &i2c_bus));

    // Keep both devices deselected while their shared clock/data pins change
    // ownership from LCD register IO to the screenshot worker's SD SPI host.
    gpio_config_t selects{};
    selects.pin_bit_mask = (1ULL << kLcdCs) | (1ULL << 42);
    selects.mode = GPIO_MODE_OUTPUT;
    ESP_ERROR_CHECK(gpio_config(&selects));
    ESP_ERROR_CHECK(gpio_set_level(static_cast<gpio_num_t>(kLcdCs), 1));
    ESP_ERROR_CHECK(gpio_set_level(GPIO_NUM_42, 1));

    ledc_timer_config_t timer{};
    timer.speed_mode = LEDC_LOW_SPEED_MODE;
    timer.duty_resolution = LEDC_TIMER_10_BIT;
    timer.timer_num = LEDC_TIMER_0;
    timer.freq_hz = 5000;
    timer.clk_cfg = LEDC_AUTO_CLK;
    ESP_ERROR_CHECK(ledc_timer_config(&timer));
    ledc_channel_config_t backlight{};
    backlight.gpio_num = kBacklight;
    backlight.speed_mode = LEDC_LOW_SPEED_MODE;
    backlight.channel = LEDC_CHANNEL_0;
    backlight.timer_sel = LEDC_TIMER_0;
    ESP_ERROR_CHECK(ledc_channel_config(&backlight));
}


bool board_display_start()
{
    spi_line_config_t lines{};
    lines.cs_io_type = IO_TYPE_GPIO;
    lines.cs_gpio_num = kLcdCs;
    lines.scl_io_type = IO_TYPE_GPIO;
    lines.scl_gpio_num = kSharedClock;
    lines.sda_io_type = IO_TYPE_GPIO;
    lines.sda_gpio_num = kSharedMosi;
    esp_lcd_panel_io_3wire_spi_config_t io_config = ST7701_PANEL_IO_3WIRE_SPI_CONFIG(lines, 0);
    esp_lcd_panel_io_handle_t io = nullptr;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_3wire_spi(&io_config, &io));

    esp_lcd_rgb_panel_config_t rgb{};
    rgb.clk_src = LCD_CLK_SRC_DEFAULT;
    rgb.data_width = 16;
    rgb.in_color_format = LCD_COLOR_FMT_RGB565;
    rgb.out_color_format = LCD_COLOR_FMT_RGB565;
    rgb.num_fbs = 2;
    rgb.bounce_buffer_size_px = kResolution * 10;
    rgb.dma_burst_size = 64;
    rgb.hsync_gpio_num = GPIO_NUM_16;
    rgb.vsync_gpio_num = GPIO_NUM_17;
    rgb.de_gpio_num = GPIO_NUM_18;
    rgb.pclk_gpio_num = GPIO_NUM_21;
    rgb.disp_gpio_num = GPIO_NUM_NC;
    // RGB565 data bus is B0..B4, G0..G5, R0..R4, least significant first.
    const gpio_num_t pins[] = {GPIO_NUM_4, GPIO_NUM_5, GPIO_NUM_6, GPIO_NUM_7, GPIO_NUM_15,
        GPIO_NUM_8, GPIO_NUM_20, GPIO_NUM_3, GPIO_NUM_46, GPIO_NUM_9, GPIO_NUM_10,
        GPIO_NUM_11, GPIO_NUM_12, GPIO_NUM_13, GPIO_NUM_14, GPIO_NUM_0};
    std::copy_n(pins, 16, rgb.data_gpio_nums);
    rgb.flags.fb_in_psram = true;
    // Conservative clock/edges from Arduino_GFX's GUITION type 9 preset.
    rgb.timings.pclk_hz = 12 * 1000 * 1000;
    rgb.timings.h_res = rgb.timings.v_res = kResolution;
    rgb.timings.hsync_front_porch = 10;
    rgb.timings.hsync_pulse_width = 8;
    rgb.timings.hsync_back_porch = 50;
    rgb.timings.vsync_front_porch = 10;
    rgb.timings.vsync_pulse_width = 8;
    rgb.timings.vsync_back_porch = 20;
    rgb.timings.flags.hsync_idle_low = false;
    rgb.timings.flags.vsync_idle_low = false;
    rgb.timings.flags.pclk_active_neg = false;
    st7701_vendor_config_t vendor{};
    vendor.rgb_config = &rgb;
    vendor.init_cmds = esp32_4848s040_panel_commands(&vendor.init_cmds_size);
    // Initialize ST7701 and release the 3-wire IO before SD reuses GPIO47/48.
    // CS stays inactive. No LCD register operations are permitted afterwards.
    vendor.flags.auto_del_panel_io = true;
    esp_lcd_panel_dev_config_t panel_config{};
    panel_config.reset_gpio_num = GPIO_NUM_NC;
    // Match the vendor demo's actual MADCTL=0x00. Its legacy "bgr=true"
    // parameter emits 0x00, not the 0x08 used by IDF's BGR enum.
    panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
    // The panel's RGB666 input mode is independent of the MCU's RGB565
    // framebuffer/bus. Match the vendor's 0x3A=0x60 register sequence.
    panel_config.bits_per_pixel = 18;
    panel_config.vendor_config = &vendor;
    esp_lcd_panel_handle_t panel = nullptr;
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7701(io, &panel_config, &panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
    panel_ready = true;

    esp_lcd_touch_handle_t touch = create_touch();
    if (!touch) return false;
    lvgl_port_cfg_t port = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_ERROR_CHECK(lvgl_port_init(&port));
    lvgl_port_display_cfg_t display_config{};
    display_config.panel_handle = panel;
    display_config.buffer_size = kResolution * kResolution;
    display_config.double_buffer = true;
    display_config.hres = display_config.vres = kResolution;
    display_config.color_format = LV_COLOR_FORMAT_RGB565;
    display_config.flags.direct_mode = true;
    // LVGL uses the two driver-owned PSRAM framebuffers; no extra full frames.
    lvgl_port_display_rgb_cfg_t rgb_config{};
    rgb_config.flags.bb_mode = true;
    rgb_config.flags.avoid_tearing = true;
    lv_display_t *display = lvgl_port_add_disp_rgb(&display_config, &rgb_config);
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
bool board_display_is_round() { return false; }
bool board_speaker_is_mono() { return true; }

const char *board_sdcard_path() { return "/sdcard"; }
esp_err_t board_sdcard_mount()
{
    // Only the screenshot worker calls these functions, after display startup.
    if (!panel_ready || sdcard) return ESP_ERR_INVALID_STATE;
    spi_bus_config_t bus{};
    bus.sclk_io_num = kSharedClock;
    bus.mosi_io_num = kSharedMosi;
    bus.miso_io_num = GPIO_NUM_41;
    bus.quadwp_io_num = bus.quadhd_io_num = -1;
    bus.data4_io_num = bus.data5_io_num = bus.data6_io_num = bus.data7_io_num = -1;
    esp_err_t result = spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO);
    if (result != ESP_OK) return result;
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI2_HOST;
    sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot.host_id = SPI2_HOST;
    slot.gpio_cs = GPIO_NUM_42;
    esp_vfs_fat_sdmmc_mount_config_t config{};
    config.format_if_mount_failed = false;
    config.max_files = 2;
    result = esp_vfs_fat_sdspi_mount(board_sdcard_path(), &host, &slot, &config, &sdcard);
    if (result != ESP_OK) {
        sdcard = nullptr;
        spi_bus_free(SPI2_HOST);
    }
    return result;
}
esp_err_t board_sdcard_unmount()
{
    if (!sdcard) return ESP_ERR_INVALID_STATE;
    const esp_err_t result = esp_vfs_fat_sdcard_unmount(board_sdcard_path(), sdcard);
    if (result != ESP_OK) return result;
    sdcard = nullptr;
    return spi_bus_free(SPI2_HOST);
}
} // namespace p4radio
