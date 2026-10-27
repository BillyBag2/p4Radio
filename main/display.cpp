#include "radio.hpp"
#include "button_audio.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cmath>
#include "esp_log.h"
#include "esp_timer.h"

namespace p4radio {

lv_obj_t *g_wake_overlay = nullptr;
lv_obj_t *g_station_label = nullptr;
lv_obj_t *g_hostname_label = nullptr;
lv_obj_t *g_setup_screen = nullptr;
lv_obj_t *g_setup_qr = nullptr;
lv_obj_t *g_setup_url = nullptr;
lv_obj_t *g_setup_title = nullptr;
lv_obj_t *g_status_label = nullptr;
lv_obj_t *g_volume_bar = nullptr;
lv_obj_t *g_buffer_bar = nullptr;
lv_obj_t *g_bandwidth_needle = nullptr;
lv_obj_t *g_power_meter = nullptr;
namespace {
bool manual_settings_qr = false;
bool automatic_qr_dismissed = false;
std::string displayed_qr;
}

void show_settings_qr(lv_event_t *)
{
    if (g_sleep_requested.load()) return;
    request_button_sound();
    manual_settings_qr = true;
    const std::string url = "http://" + hostname_copy() + ".local/";
    if (lv_qrcode_update(g_setup_qr, url.data(), url.size()) == LV_RESULT_OK) displayed_qr = url;
    else ESP_LOGE(kTag, "Settings QR generation failed");
    lv_label_set_text(g_setup_url, (url + "\n\nTap anywhere to return").c_str());
    lv_obj_set_style_bg_color(g_setup_screen, lv_color_hex(0x4a3008), 0);
    lv_obj_remove_flag(g_setup_screen, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(g_setup_screen);
}

void dismiss_setup_qr(lv_event_t *)
{
    request_button_sound();
    manual_settings_qr = false;
    automatic_qr_dismissed = true;
    lv_obj_add_flag(g_setup_screen, LV_OBJ_FLAG_HIDDEN);
}
constexpr int kMeterWidth = 280;
constexpr int kMeterHeight = 28;
constexpr int kNeedleWidth = 4;
constexpr int kMeterMargin = 12;

void wake_on_tap(lv_event_t *)
{
    if (!g_sleep_requested.load()) return;
    // The full-screen overlay consumes this tap, so it cannot activate a control.
    if (g_wake_overlay) lv_obj_add_flag(g_wake_overlay, LV_OBJ_FLAG_HIDDEN);
    const esp_err_t result = bsp_display_brightness_set(100);
    if (result != ESP_OK) ESP_LOGE(kTag, "Backlight restore failed: %s", esp_err_to_name(result));
    request_button_sound();
    g_resume_requested.store(true);
    set_status("Resuming radio...");
}

void radio_button(lv_event_t *event)
{
    request_button_sound();
    const intptr_t action = reinterpret_cast<intptr_t>(lv_event_get_user_data(event));
    switch (action) {
    case 0: // previous station
        g_station.store((g_station.load() + kStationCount - 1) % kStationCount);
        g_playback_requested.store(true);
        request_preferences_save();
        break;
    case 1: // next station
        g_station.store((g_station.load() + 1) % kStationCount);
        g_playback_requested.store(true);
        request_preferences_save();
        break;
    case 2:
        g_volume.store(std::max(kMinVolume, g_volume.load() - 1));
        request_preferences_save();
        break;
    case 3:
        g_volume.store(std::min(kMaxVolume, g_volume.load() + 1));
        request_preferences_save();
        break;
    case 5:
        g_scan_requested.store(true);
        set_status("Stopping stream for a fresh search");
        break;
    case 4:
        g_resume_requested.store(false);
        g_idle_ready.store(false);
        if (g_wake_overlay) {
            lv_obj_remove_flag(g_wake_overlay, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(g_wake_overlay);
        }
        if (bsp_display_backlight_off() != ESP_OK) ESP_LOGE(kTag, "Backlight off failed");
        g_playback_requested.store(false);
        g_sleep_requested.store(true);
        break;
    }
}

lv_obj_t *add_button(lv_obj_t *parent, const char *caption, int x, int y, int width, int height, intptr_t action)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_set_size(button, width, height);
    lv_obj_set_pos(button, x, y);
    lv_obj_set_style_bg_color(button, lv_color_hex(action == 4 ? 0x9a4b53 : 0x287b71), 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(action == 4 ? 0x461d24 : 0x103e38), LV_STATE_PRESSED);
    lv_obj_set_style_border_color(button, lv_color_white(), 0);
    lv_obj_set_style_border_width(button, 2, 0);
    lv_obj_set_style_border_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(button, 18, 0);
    lv_obj_add_event_cb(button, radio_button, LV_EVENT_CLICKED, reinterpret_cast<void *>(action));
    lv_obj_t *label = lv_label_create(button);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_40, 0);
    lv_label_set_text(label, caption);
    lv_obj_center(label);
    return button;
}

void refresh_ui(lv_timer_t *)
{
    const std::string hostname = hostname_copy();
    if (g_hostname_label) lv_label_set_text(g_hostname_label, ("http://" + hostname + ".local/").c_str());
    if (g_setup_title) lv_label_set_text(g_setup_title, hostname.c_str());
    static int previous_automatic_stage = 0;
    const int automatic_stage = g_portal_active.load() && !g_setup_submitted.load() ?
        (g_ap_clients.load() > 0 ? 2 : 1) : 0;
    const bool automatic_changed = automatic_stage != previous_automatic_stage;
    if (automatic_changed) automatic_qr_dismissed = false;
    previous_automatic_stage = automatic_stage;
    if (g_setup_screen) {
        if (manual_settings_qr || (automatic_stage && !automatic_qr_dismissed)) {
            const bool wifi_join_qr = !manual_settings_qr && automatic_stage == 1;
            const std::string url = "http://" + hostname + ".local/" + (manual_settings_qr ? "" : "wifi");
            const std::string payload = wifi_join_qr ? "WIFI:T:nopass;S:p4Radio-Setup;;" : url;
            // Regenerate only when the mode or hostname changes.
            if (payload != displayed_qr) {
                if (lv_qrcode_update(g_setup_qr, payload.data(), payload.size()) == LV_RESULT_OK)
                    displayed_qr = payload;
                else ESP_LOGE(kTag, "Setup QR code generation failed");
            }
            lv_obj_set_style_bg_color(g_setup_screen, lv_color_hex(wifi_join_qr ? 0x480f18 : 0x4a3008), 0);
            if (wifi_join_qr) lv_label_set_text(g_setup_url, "Scan to join p4Radio-Setup\n\nTap anywhere to return");
            else if (manual_settings_qr) lv_label_set_text(g_setup_url, (url + "\n\nTap anywhere to return").c_str());
            else lv_label_set_text(g_setup_url, ("Scan to open " + url + "\nOr http://192.168.4.1/wifi\nTap anywhere to return").c_str());
            if (automatic_changed && automatic_stage && !manual_settings_qr && !g_sleep_requested.load())
                request_disconnect_sound();
            lv_obj_remove_flag(g_setup_screen, LV_OBJ_FLAG_HIDDEN);
            if (!g_sleep_requested.load()) lv_obj_move_foreground(g_setup_screen);
        } else lv_obj_add_flag(g_setup_screen, LV_OBJ_FLAG_HIDDEN);
    }
    if (g_station_label) lv_label_set_text(g_station_label, kStations[g_station.load()].name);
    if (g_status_label) {
        std::string text = status_copy();
        if (text.rfind("Playing ", 0) == 0) {
            const int bitrate = g_stream_kbps.load();
            const int channels = g_stream_channels.load();
            text += " - " + (bitrate > 0 ? std::to_string(bitrate) : std::string("--")) + " kbit/s";
            text += channels == 1 ? " - mono" : channels == 2 ? " - stereo" : " - --";
        }
        lv_label_set_text(g_status_label, text.c_str());
    }
    if (g_buffer_bar) {
        if (g_show_buffer_bar.load()) lv_obj_remove_flag(g_buffer_bar, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(g_buffer_bar, LV_OBJ_FLAG_HIDDEN);
        const int fill = g_buffer_percent.load();
        lv_bar_set_value(g_buffer_bar, fill, LV_ANIM_OFF);
        const int low_threshold = 100 / kAudioBufferSeconds; // One second remaining.
        lv_obj_set_style_bg_color(g_buffer_bar, lv_color_hex(fill < low_threshold ? 0xffa020 : 0x309cff), LV_PART_INDICATOR);
    }
    if (g_volume_bar) {
        if (g_show_volume_bar.load()) lv_obj_remove_flag(g_volume_bar, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(g_volume_bar, LV_OBJ_FLAG_HIDDEN);
        lv_bar_set_value(g_volume_bar, g_volume.load(), LV_ANIM_OFF);
    }
    if (g_power_meter) {
        if (g_show_power_meter.load()) lv_obj_remove_flag(g_power_meter, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(g_power_meter, LV_OBJ_FLAG_HIDDEN);
    }
    if (g_bandwidth_needle) {
        const int stream = g_stream_kbps.load();
        const int wifi = g_download_kbps.load();
        const bool fresh = g_connected.load() &&
            esp_timer_get_time() - g_download_time.load() < 15000000;
        float value = -3.0f;
        if (fresh && stream > 0 && wifi > 0)
            value = std::clamp(std::log2(static_cast<float>(wifi) / stream), -3.0f, 3.0f);
        const int travel = kMeterWidth - 2 * kMeterMargin - kNeedleWidth;
        const int x = kMeterMargin + static_cast<int>(std::lround((value + 3.0f) / 6.0f * travel));
        lv_obj_set_x(g_bandwidth_needle, x);
    }

}

void build_ui()
{
    lv_display_t *display = bsp_display_start();
    if (!display) {
        ESP_LOGE(kTag, "Board display initialization failed");
        return;
    }
    bsp_display_brightness_set(100);
    if (!bsp_display_lock(-1)) return;
    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x101827), 0);
    lv_obj_set_style_text_color(screen, lv_color_hex(0xf4f7fb), 0);

    lv_obj_t *brand = lv_label_create(screen);
    g_hostname_label = brand;
    lv_label_set_text(brand, ("http://" + hostname_copy() + ".local/").c_str());
    lv_obj_set_style_text_decor(brand, LV_TEXT_DECOR_UNDERLINE, 0);
    lv_obj_add_flag(brand, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(brand, show_settings_qr, LV_EVENT_CLICKED, nullptr);
    lv_obj_set_style_text_color(brand, lv_color_hex(0x60d6ad), 0);
    lv_obj_set_style_text_font(brand, &lv_font_montserrat_24, 0);
    lv_obj_set_width(brand, 640);
    lv_obj_set_style_text_align(brand, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(brand, LV_LABEL_LONG_DOT);
    lv_obj_align(brand, LV_ALIGN_TOP_MID, 0, 40);

    g_station_label = lv_label_create(screen);
    lv_label_set_text(g_station_label, kStations[g_station.load()].name);
    lv_obj_set_style_text_font(g_station_label, &lv_font_montserrat_40, 0);
    lv_obj_set_width(g_station_label, 650);
    lv_obj_set_style_text_align(g_station_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(g_station_label, LV_LABEL_LONG_DOT);
    lv_obj_align(g_station_label, LV_ALIGN_TOP_MID, 0, 145);

    g_status_label = lv_label_create(screen);
    lv_label_set_text(g_status_label, "Starting radio");
    lv_obj_set_style_text_color(g_status_label, lv_color_hex(0xa9b8ca), 0);
    lv_obj_set_width(g_status_label, 650);
    lv_obj_set_style_text_align(g_status_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(g_status_label, LV_ALIGN_TOP_MID, 0, 230);

    // Ratio meter: red below parity, green above parity. No numeric scale.
    lv_obj_t *meter = lv_obj_create(screen);
    g_power_meter = meter;
    if (!g_show_power_meter.load()) lv_obj_add_flag(meter, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_style_all(meter);
    lv_obj_set_size(meter, kMeterWidth, kMeterHeight);
    lv_obj_align(meter, LV_ALIGN_TOP_MID, 0, 280);
    lv_obj_remove_flag(meter, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(meter, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(meter, lv_color_hex(0xff2020), 0);
    lv_obj_set_style_bg_opa(meter, LV_OPA_COVER, 0);
    lv_obj_t *green = lv_obj_create(meter);
    lv_obj_remove_style_all(green);
    lv_obj_set_size(green, kMeterWidth / 2, kMeterHeight);
    lv_obj_set_pos(green, kMeterWidth / 2, 0);
    lv_obj_set_style_bg_color(green, lv_color_hex(0x20ef40), 0);
    lv_obj_set_style_bg_opa(green, LV_OPA_COVER, 0);
    lv_obj_remove_flag(green, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(green, LV_OBJ_FLAG_CLICKABLE);
    g_bandwidth_needle = lv_obj_create(meter);
    lv_obj_remove_style_all(g_bandwidth_needle);
    lv_obj_set_size(g_bandwidth_needle, kNeedleWidth, kMeterHeight);
    lv_obj_set_pos(g_bandwidth_needle, kMeterMargin, 0);
    lv_obj_set_style_bg_color(g_bandwidth_needle, lv_color_hex(0x18202c), 0);
    lv_obj_set_style_bg_opa(g_bandwidth_needle, LV_OPA_COVER, 0);
    lv_obj_remove_flag(g_bandwidth_needle, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(g_bandwidth_needle, LV_OBJ_FLAG_CLICKABLE);

    // Draw the outline above both colored halves and the needle without
    // changing the content dimensions used to position the needle.
    lv_obj_t *meter_border = lv_obj_create(meter);
    lv_obj_remove_style_all(meter_border);
    lv_obj_set_size(meter_border, kMeterWidth, kMeterHeight);
    lv_obj_set_pos(meter_border, 0, 0);
    lv_obj_set_style_border_color(meter_border, lv_color_white(), 0);
    lv_obj_set_style_border_width(meter_border, 2, 0);
    lv_obj_set_style_border_opa(meter_border, LV_OPA_COVER, 0);
    lv_obj_remove_flag(meter_border, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(meter_border, LV_OBJ_FLAG_CLICKABLE);

    g_buffer_bar = lv_bar_create(screen);
    lv_obj_set_size(g_buffer_bar, 8, 620);
    lv_obj_align(g_buffer_bar, LV_ALIGN_LEFT_MID, 6, 0);
    if (!g_show_buffer_bar.load()) lv_obj_add_flag(g_buffer_bar, LV_OBJ_FLAG_HIDDEN);
    lv_bar_set_range(g_buffer_bar, 0, 100);
    lv_bar_set_value(g_buffer_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(g_buffer_bar, lv_color_hex(0x1b2638), LV_PART_MAIN);
    lv_obj_set_style_bg_color(g_buffer_bar, lv_color_hex(0x309cff), LV_PART_INDICATOR);

    g_volume_bar = lv_bar_create(screen);
    lv_obj_set_size(g_volume_bar, 8, 620);
    lv_obj_align(g_volume_bar, LV_ALIGN_RIGHT_MID, -6, 0);
    if (!g_show_volume_bar.load()) lv_obj_add_flag(g_volume_bar, LV_OBJ_FLAG_HIDDEN);
    lv_bar_set_range(g_volume_bar, 0, 10);
    lv_bar_set_value(g_volume_bar, g_volume.load(), LV_ANIM_OFF);
    lv_obj_set_style_bg_color(g_volume_bar, lv_color_hex(0x1b2638), LV_PART_MAIN);
    lv_obj_set_style_bg_color(g_volume_bar, lv_color_hex(0x60d6ad), LV_PART_INDICATOR);

    add_button(screen, "<", 100, 330, 200, 110, 0);
    add_button(screen, ">", 420, 330, 200, 110, 1);
    add_button(screen, "-", 100, 485, 200, 95, 2);
    add_button(screen, "+", 420, 485, 200, 95, 3);

    add_button(screen, "OFF", 100, 635, 220, 64, 4);
    add_button(screen, "TUNE", 400, 635, 220, 64, 5);

    g_setup_screen = lv_obj_create(screen);
    lv_obj_add_flag(g_setup_screen, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(g_setup_screen, dismiss_setup_qr, LV_EVENT_CLICKED, nullptr);
    lv_obj_set_size(g_setup_screen, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(g_setup_screen, 0, 0);
    lv_obj_set_style_bg_color(g_setup_screen, lv_color_hex(0x101827), 0);
    lv_obj_set_style_bg_opa(g_setup_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(g_setup_screen, lv_color_hex(0xf4f7fb), 0);
    lv_obj_set_style_border_width(g_setup_screen, 0, 0);
    lv_obj_remove_flag(g_setup_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(g_setup_screen, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t *setup_title = lv_label_create(g_setup_screen);
    g_setup_title = setup_title;
    lv_obj_remove_flag(setup_title, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_text_color(setup_title, lv_color_hex(0xf4f7fb), 0);
    lv_label_set_text(setup_title, hostname_copy().c_str());
    lv_obj_set_width(setup_title, 640);
    lv_obj_set_style_text_align(setup_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(setup_title, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(setup_title, &lv_font_montserrat_40, 0);
    lv_obj_align(setup_title, LV_ALIGN_TOP_MID, 0, 50);
    g_setup_qr = lv_qrcode_create(g_setup_screen);
    lv_obj_remove_flag(g_setup_qr, LV_OBJ_FLAG_CLICKABLE);
    lv_qrcode_set_size(g_setup_qr, 320);
    lv_qrcode_set_dark_color(g_setup_qr, lv_color_black());
    lv_qrcode_set_light_color(g_setup_qr, lv_color_white());
    lv_obj_set_style_pad_all(g_setup_qr, 20, 0);
    lv_obj_set_style_bg_color(g_setup_qr, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(g_setup_qr, LV_OPA_COVER, 0);
    lv_obj_align(g_setup_qr, LV_ALIGN_CENTER, 0, -20);
    constexpr char wifi_qr[] = "WIFI:T:nopass;S:p4Radio-Setup;;";
    if (lv_qrcode_update(g_setup_qr, wifi_qr, sizeof(wifi_qr) - 1) != LV_RESULT_OK)
        ESP_LOGE(kTag, "Wi-Fi QR code generation failed");
    g_setup_url = lv_label_create(g_setup_screen);
    lv_obj_remove_flag(g_setup_url, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_text_color(g_setup_url, lv_color_hex(0xf4f7fb), 0);
    lv_obj_set_width(g_setup_url, 640);
    lv_obj_set_style_text_font(g_setup_url, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_align(g_setup_url, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(g_setup_url, LV_ALIGN_BOTTOM_MID, 0, -60);

    g_wake_overlay = lv_obj_create(screen);
    lv_obj_set_size(g_wake_overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(g_wake_overlay, 0, 0);
    lv_obj_set_style_bg_color(g_wake_overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(g_wake_overlay, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_wake_overlay, 0, 0);
    lv_obj_set_style_radius(g_wake_overlay, 0, 0);
    lv_obj_remove_flag(g_wake_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(g_wake_overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(g_wake_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(g_wake_overlay, wake_on_tap, LV_EVENT_CLICKED, nullptr);

    lv_timer_create(refresh_ui, 500, nullptr);
    bsp_display_unlock();
}


} // namespace p4radio
