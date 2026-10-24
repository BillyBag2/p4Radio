#include "radio.hpp"
#include "button_audio.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
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
lv_obj_t *g_rates_label = nullptr;

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
    if (g_hostname_label) lv_label_set_text(g_hostname_label, hostname.c_str());
    if (g_setup_title) lv_label_set_text(g_setup_title, hostname.c_str());
    if (g_setup_screen) {
        if (g_portal_active.load() && !g_setup_submitted.load()) {
            lv_obj_remove_flag(g_setup_screen, LV_OBJ_FLAG_HIDDEN);
            if (!g_sleep_requested.load()) lv_obj_move_foreground(g_setup_screen);
            // Only regenerate when the client state or saved hostname changes.
            static std::string displayed_qr;
            const bool client_connected = g_ap_clients.load() > 0;
            const std::string qr_payload = client_connected ? "http://" + hostname + ".local/" :
                "WIFI:T:nopass;S:p4Radio-Setup;;";
            if (qr_payload != displayed_qr) {
                if (lv_qrcode_update(g_setup_qr, qr_payload.data(), qr_payload.size()) == LV_RESULT_OK)
                    displayed_qr = qr_payload;
                else ESP_LOGE(kTag, "Setup QR code generation failed");
            }
            if (!client_connected) {
                lv_obj_remove_flag(g_setup_qr, LV_OBJ_FLAG_HIDDEN);
                lv_label_set_text(g_setup_url, "Scan to join p4Radio-Setup");
            } else {
                lv_obj_remove_flag(g_setup_qr, LV_OBJ_FLAG_HIDDEN);
                lv_label_set_text(g_setup_url, ("Scan to open http://" + hostname + ".local/\n\nOr http://192.168.4.1/").c_str());
            }
        } else lv_obj_add_flag(g_setup_screen, LV_OBJ_FLAG_HIDDEN);
    }
    if (g_station_label) lv_label_set_text(g_station_label, kStations[g_station.load()].name);
    if (g_status_label) {
        const std::string text = status_copy();
        lv_label_set_text(g_status_label, text.c_str());
    }
    if (g_volume_bar) lv_bar_set_value(g_volume_bar, g_volume.load(), LV_ANIM_OFF);
    if (g_rates_label) {
        char rates[100];
        const int stream = g_stream_kbps.load();
        const int wifi = g_connected.load() && esp_timer_get_time() - g_download_time.load() < 15000000 ? g_download_kbps.load() : 0;
        char a[16], b[16];
        if (stream > 0) std::snprintf(a, sizeof(a), "%d", stream); else std::snprintf(a, sizeof(a), "--");
        if (wifi > 0) std::snprintf(b, sizeof(b), "%d", wifi); else std::snprintf(b, sizeof(b), "--");
        std::snprintf(rates, sizeof(rates), "STREAM %s kbit/s | Wi-Fi RX %s kbit/s", a, b);
        lv_label_set_text(g_rates_label, rates);
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
    lv_label_set_text(brand, hostname_copy().c_str());
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

    g_rates_label = lv_label_create(screen);
    lv_obj_set_style_text_font(g_rates_label, &lv_font_montserrat_24, 0);
    lv_label_set_text(g_rates_label, "STREAM -- kbit/s | Wi-Fi RX -- kbit/s");
    lv_obj_align(g_rates_label, LV_ALIGN_TOP_MID, 0, 280);

    g_volume_bar = lv_bar_create(screen);
    lv_obj_set_size(g_volume_bar, 8, 620);
    lv_obj_align(g_volume_bar, LV_ALIGN_RIGHT_MID, -6, 0);
    lv_bar_set_range(g_volume_bar, 0, 10);
    lv_bar_set_value(g_volume_bar, g_volume.load(), LV_ANIM_OFF);
    lv_obj_set_style_bg_color(g_volume_bar, lv_color_hex(0x1b2638), LV_PART_MAIN);
    lv_obj_set_style_bg_color(g_volume_bar, lv_color_hex(0x60d6ad), LV_PART_INDICATOR);

    add_button(screen, "<", 100, 330, 200, 110, 0);
    add_button(screen, ">", 420, 330, 200, 110, 1);
    add_button(screen, "-", 100, 485, 200, 95, 2);
    add_button(screen, "+", 420, 485, 200, 95, 3);

    add_button(screen, "STOP", 100, 635, 220, 64, 4);
    add_button(screen, "SCAN", 400, 635, 220, 64, 5);

    g_setup_screen = lv_obj_create(screen);
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
    lv_obj_set_style_text_color(setup_title, lv_color_hex(0xf4f7fb), 0);
    lv_label_set_text(setup_title, hostname_copy().c_str());
    lv_obj_set_width(setup_title, 640);
    lv_obj_set_style_text_align(setup_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(setup_title, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(setup_title, &lv_font_montserrat_40, 0);
    lv_obj_align(setup_title, LV_ALIGN_TOP_MID, 0, 50);
    g_setup_qr = lv_qrcode_create(g_setup_screen);
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
