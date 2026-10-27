#include "display_skin.hpp"
#include "radio.hpp"
#include "button_audio.hpp"
#include <algorithm>
#include <cmath>
#include "esp_log.h"
#include "esp_timer.h"
// LVGL 9.3 exposes the hit-test callback payload in this header.
#include "src/core/lv_obj_event_private.h"

namespace p4radio {

void DisplaySkin::show_settings_qr()
{
    if (g_sleep_requested.load()) return;
    request_button_sound();
    manual_settings_qr_ = true;
    const std::string url = "http://" + hostname_copy() + ".local/";
    if (lv_qrcode_update(setup_qr_, url.data(), url.size()) == LV_RESULT_OK) displayed_qr_ = url;
    else ESP_LOGE(kTag, "Settings QR generation failed");
    lv_label_set_text(setup_url_, (url + "\n\nTap anywhere to return").c_str());
    lv_obj_set_style_bg_color(setup_screen_, lv_color_hex(0x4a3008), 0);
    lv_obj_remove_flag(setup_screen_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(setup_screen_);
}

void DisplaySkin::dismiss_setup_qr()
{
    request_button_sound();
    manual_settings_qr_ = false;
    automatic_qr_dismissed_ = true;
    lv_obj_add_flag(setup_screen_, LV_OBJ_FLAG_HIDDEN);
}

void DisplaySkin::wake_on_tap()
{
    if (!g_sleep_requested.load()) return;
    // The full-screen overlay consumes this tap, so it cannot activate a control.
    if (wake_overlay_) lv_obj_add_flag(wake_overlay_, LV_OBJ_FLAG_HIDDEN);
    const esp_err_t result = bsp_display_brightness_set(100);
    if (result != ESP_OK) ESP_LOGE(kTag, "Backlight restore failed: %s", esp_err_to_name(result));
    request_button_sound();
    g_resume_requested.store(true);
    set_status("Resuming radio...");
}

void DisplaySkin::radio_button(Action action)
{
    request_button_sound();
    switch (action) {
    case Action::PreviousStation: // previous station
        g_station.store((g_station.load() + kStationCount - 1) % kStationCount);
        g_playback_requested.store(true);
        request_preferences_save();
        break;
    case Action::NextStation: // next station
        g_station.store((g_station.load() + 1) % kStationCount);
        g_playback_requested.store(true);
        request_preferences_save();
        break;
    case Action::VolumeDown:
        g_volume.store(std::max(kMinVolume, g_volume.load() - 1));
        request_preferences_save();
        break;
    case Action::VolumeUp:
        g_volume.store(std::min(kMaxVolume, g_volume.load() + 1));
        request_preferences_save();
        break;
    case Action::Tune:
        g_scan_requested.store(true);
        set_status("Stopping stream for a fresh search");
        break;
    case Action::Off:
        g_resume_requested.store(false);
        g_idle_ready.store(false);
        if (wake_overlay_) {
            lv_obj_remove_flag(wake_overlay_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(wake_overlay_);
        }
        if (bsp_display_backlight_off() != ESP_OK) ESP_LOGE(kTag, "Backlight off failed");
        g_playback_requested.store(false);
        g_sleep_requested.store(true);
        break;
    case Action::Count:
        break;
    }
}

lv_obj_t *DisplaySkin::add_button(const char *caption, int x, int y, int width, int height, Action action,
                                 CaptionAlignment alignment)
{
    lv_obj_t *button = lv_button_create(surface_);
    lv_obj_set_size(button, width, height);
    lv_obj_set_pos(button, x, y);
    lv_obj_set_style_bg_color(button, lv_color_hex(action == Action::Off ? 0x9a4b53 : 0x287b71), 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(action == Action::Off ? 0x461d24 : 0x103e38), LV_STATE_PRESSED);
    lv_obj_set_style_border_color(button, lv_color_white(), 0);
    lv_obj_set_style_border_width(button, 2, 0);
    lv_obj_set_style_border_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(button, 18, 0);
    lv_obj_set_style_pad_all(button, 8, 0);
    const size_t index = static_cast<size_t>(action);
    buttons_[index] = {this, action};
    lv_obj_add_event_cb(button, button_event, LV_EVENT_CLICKED, &buttons_[index]);
    constrain_touch(button);
    lv_obj_t *label = lv_label_create(button);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_48, 0);
    lv_label_set_text(label, caption);
    if (index < static_cast<size_t>(Action::Off)) {
        // LVGL scale 256 is 1x: enlarge only station/volume symbols to 2x.
        lv_obj_set_style_transform_pivot_x(label, LV_PCT(50), 0);
        lv_obj_set_style_transform_pivot_y(label, LV_PCT(50), 0);
        lv_obj_set_style_transform_scale_x(label, 512, 0);
        lv_obj_set_style_transform_scale_y(label, 512, 0);
    }
    lv_obj_remove_flag(label, LV_OBJ_FLAG_CLICKABLE);
    if (alignment == CaptionAlignment::Right) lv_obj_align(label, LV_ALIGN_RIGHT_MID, -30, 0);
    else if (alignment == CaptionAlignment::Left) lv_obj_align(label, LV_ALIGN_LEFT_MID, 30, 0);
    else lv_obj_center(label);
    return button;
}

void DisplaySkin::refresh()
{
    const std::string hostname = hostname_copy();
    if (hostname_label_) lv_label_set_text(hostname_label_, ("http://" + hostname + ".local/").c_str());
    if (setup_title_) lv_label_set_text(setup_title_, hostname.c_str());
    const int automatic_stage = g_portal_active.load() && !g_setup_submitted.load() ?
        (g_ap_clients.load() > 0 ? 2 : 1) : 0;
    const bool automatic_changed = automatic_stage != previous_automatic_stage_;
    if (automatic_changed) automatic_qr_dismissed_ = false;
    previous_automatic_stage_ = automatic_stage;
    if (setup_screen_) {
        if (manual_settings_qr_ || (automatic_stage && !automatic_qr_dismissed_)) {
            const bool wifi_join_qr = !manual_settings_qr_ && automatic_stage == 1;
            const std::string url = "http://" + hostname + ".local/" + (manual_settings_qr_ ? "" : "wifi");
            const std::string payload = wifi_join_qr ? "WIFI:T:nopass;S:p4Radio-Setup;;" : url;
            // Regenerate only when the mode or hostname changes.
            if (payload != displayed_qr_) {
                if (lv_qrcode_update(setup_qr_, payload.data(), payload.size()) == LV_RESULT_OK)
                    displayed_qr_ = payload;
                else ESP_LOGE(kTag, "Setup QR code generation failed");
            }
            lv_obj_set_style_bg_color(setup_screen_, lv_color_hex(wifi_join_qr ? 0x480f18 : 0x4a3008), 0);
            if (wifi_join_qr) lv_label_set_text(setup_url_, "Scan to join p4Radio-Setup\n\nTap anywhere to return");
            else if (manual_settings_qr_) lv_label_set_text(setup_url_, (url + "\n\nTap anywhere to return").c_str());
            else lv_label_set_text(setup_url_, ("Scan to open " + url + "\nOr http://192.168.4.1/wifi\nTap anywhere to return").c_str());
            if (automatic_changed && automatic_stage && !manual_settings_qr_ && !g_sleep_requested.load())
                request_disconnect_sound();
            lv_obj_remove_flag(setup_screen_, LV_OBJ_FLAG_HIDDEN);
            if (!g_sleep_requested.load()) lv_obj_move_foreground(setup_screen_);
        } else lv_obj_add_flag(setup_screen_, LV_OBJ_FLAG_HIDDEN);
    }
    if (station_label_) lv_label_set_text(station_label_, kStations[g_station.load()].name);
    if (status_label_) {
        std::string text = status_copy();
        if (text.rfind("Playing ", 0) == 0) {
            text = "Playing"; // The large station title already identifies the station.
            const int bitrate = g_stream_kbps.load();
            const int channels = g_stream_channels.load();
            text += " - " + (bitrate > 0 ? std::to_string(bitrate) : std::string("--")) + " kbit/s";
            text += channels == 1 ? " - mono" : channels == 2 ? " - stereo" : " - --";
        }
        lv_label_set_text(status_label_, text.c_str());
    }
    if (buffer_bar_) {
        if (g_show_buffer_bar.load()) lv_obj_remove_flag(buffer_bar_, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(buffer_bar_, LV_OBJ_FLAG_HIDDEN);
        const int fill = g_buffer_percent.load();
        lv_bar_set_value(buffer_bar_, fill, LV_ANIM_OFF);
        const int low_threshold = 100 / kAudioBufferSeconds; // One second remaining.
        lv_obj_set_style_bg_color(buffer_bar_, lv_color_hex(fill < low_threshold ? 0xffa020 : 0x309cff), LV_PART_INDICATOR);
    }
    if (volume_bar_) {
        if (g_show_volume_bar.load()) lv_obj_remove_flag(volume_bar_, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(volume_bar_, LV_OBJ_FLAG_HIDDEN);
        lv_bar_set_value(volume_bar_, g_volume.load(), LV_ANIM_OFF);
    }
    if (power_meter_) {
        if (g_show_power_meter.load()) lv_obj_remove_flag(power_meter_, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(power_meter_, LV_OBJ_FLAG_HIDDEN);
    }
    if (bandwidth_needle_) {
        const int stream = g_stream_kbps.load();
        const int wifi = g_download_kbps.load();
        const bool fresh = g_connected.load() &&
            esp_timer_get_time() - g_download_time.load() < 15000000;
        float value = -3.0f;
        if (fresh && stream > 0 && wifi > 0)
            value = std::clamp(std::log2(static_cast<float>(wifi) / stream), -3.0f, 3.0f);
        const int travel = kMeterWidth - 2 * kMeterMargin - kNeedleWidth;
        const int x = kMeterMargin + static_cast<int>(std::lround((value + 3.0f) / 6.0f * travel));
        lv_obj_set_x(bandwidth_needle_, x);
    }

}

bool DisplaySkin::build(lv_obj_t *screen)
{
    if (!prepare_resources()) return false;
    // The panel outside the skin stays black, including QR and idle screens.
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    surface_ = lv_obj_create(screen);
    lv_obj_remove_style_all(surface_);
    lv_obj_add_flag(surface_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_size(surface_, 720, 720);
    lv_obj_center(surface_);
    lv_obj_remove_flag(surface_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(surface_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(surface_, lv_color_hex(0x101827), 0);
    lv_obj_set_style_bg_opa(surface_, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(surface_, lv_color_hex(0xf4f7fb), 0);

    lv_obj_t *brand = lv_label_create(surface_);
    hostname_label_ = brand;
    lv_label_set_text(brand, ("http://" + hostname_copy() + ".local/").c_str());
    lv_obj_set_style_text_decor(brand, LV_TEXT_DECOR_UNDERLINE, 0);
    lv_obj_add_flag(brand, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(brand, settings_event, LV_EVENT_CLICKED, this);
    constrain_touch(brand);
    lv_obj_set_style_text_color(brand, lv_color_hex(0x60d6ad), 0);
    lv_obj_set_style_text_font(brand, &lv_font_montserrat_24, 0);
    lv_obj_set_width(brand, 640);
    lv_obj_set_style_text_align(brand, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(brand, LV_LABEL_LONG_DOT);
    lv_obj_align(brand, LV_ALIGN_TOP_MID, 0, 26);

    station_label_ = lv_label_create(surface_);
    lv_label_set_text(station_label_, kStations[g_station.load()].name);
    lv_obj_set_style_text_font(station_label_, &lv_font_montserrat_48, 0);
    lv_obj_set_width(station_label_, 650);
    lv_obj_set_style_text_align(station_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(station_label_, LV_LABEL_LONG_DOT);
    lv_obj_align(station_label_, LV_ALIGN_TOP_MID, 0, 102);

    status_label_ = lv_label_create(surface_);
    lv_label_set_text(status_label_, "Starting radio");
    lv_obj_set_style_text_color(status_label_, lv_color_hex(0xc9d4e2), 0);
    lv_obj_set_style_text_font(status_label_, &lv_font_montserrat_24, 0);
    lv_obj_set_width(status_label_, 650);
    lv_obj_set_style_text_align(status_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(status_label_, LV_ALIGN_TOP_MID, 0, 170);

    // Ratio meter: red below parity, green above parity. No numeric scale.
    lv_obj_t *meter = lv_obj_create(surface_);
    power_meter_ = meter;
    if (!g_show_power_meter.load()) lv_obj_add_flag(meter, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_style_all(meter);
    lv_obj_set_size(meter, kMeterWidth, kMeterHeight);
    lv_obj_align(meter, LV_ALIGN_TOP_MID, 0, 234);
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
    bandwidth_needle_ = lv_obj_create(meter);
    lv_obj_remove_style_all(bandwidth_needle_);
    lv_obj_set_size(bandwidth_needle_, kNeedleWidth, kMeterHeight);
    lv_obj_set_pos(bandwidth_needle_, kMeterMargin, 0);
    lv_obj_set_style_bg_color(bandwidth_needle_, lv_color_hex(0x18202c), 0);
    lv_obj_set_style_bg_opa(bandwidth_needle_, LV_OPA_COVER, 0);
    lv_obj_remove_flag(bandwidth_needle_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(bandwidth_needle_, LV_OBJ_FLAG_CLICKABLE);

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

    buffer_bar_ = lv_bar_create(surface_);
    lv_obj_set_size(buffer_bar_, 8, 620);
    lv_obj_align(buffer_bar_, LV_ALIGN_LEFT_MID, 6, 0);
    if (!g_show_buffer_bar.load()) lv_obj_add_flag(buffer_bar_, LV_OBJ_FLAG_HIDDEN);
    lv_bar_set_range(buffer_bar_, 0, 100);
    lv_bar_set_value(buffer_bar_, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(buffer_bar_, lv_color_hex(0x1b2638), LV_PART_MAIN);
    lv_obj_set_style_bg_color(buffer_bar_, lv_color_hex(0x309cff), LV_PART_INDICATOR);

    volume_bar_ = lv_bar_create(surface_);
    lv_obj_set_size(volume_bar_, 8, 620);
    lv_obj_align(volume_bar_, LV_ALIGN_RIGHT_MID, -6, 0);
    if (!g_show_volume_bar.load()) lv_obj_add_flag(volume_bar_, LV_OBJ_FLAG_HIDDEN);
    lv_bar_set_range(volume_bar_, 0, 10);
    lv_bar_set_value(volume_bar_, g_volume.load(), LV_ANIM_OFF);
    lv_obj_set_style_bg_color(volume_bar_, lv_color_hex(0x1b2638), LV_PART_MAIN);
    lv_obj_set_style_bg_color(volume_bar_, lv_color_hex(0x60d6ad), LV_PART_INDICATOR);

    build_controls();

    setup_screen_ = lv_obj_create(surface_);
    lv_obj_add_flag(setup_screen_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(setup_screen_, dismiss_event, LV_EVENT_CLICKED, this);
    constrain_touch(setup_screen_);
    lv_obj_set_style_pad_all(setup_screen_, 0, 0);
    lv_obj_set_style_radius(setup_screen_, 0, 0);
    lv_obj_set_size(setup_screen_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(setup_screen_, 0, 0);
    lv_obj_set_style_bg_color(setup_screen_, lv_color_hex(0x101827), 0);
    lv_obj_set_style_bg_opa(setup_screen_, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(setup_screen_, lv_color_hex(0xf4f7fb), 0);
    lv_obj_set_style_border_width(setup_screen_, 0, 0);
    lv_obj_remove_flag(setup_screen_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(setup_screen_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t *setup_title = lv_label_create(setup_screen_);
    setup_title_ = setup_title;
    lv_obj_remove_flag(setup_title, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_text_color(setup_title, lv_color_hex(0xf4f7fb), 0);
    lv_label_set_text(setup_title, hostname_copy().c_str());
    lv_obj_set_width(setup_title, 640);
    lv_obj_set_style_text_align(setup_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(setup_title, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(setup_title, &lv_font_montserrat_40, 0);
    lv_obj_align(setup_title, LV_ALIGN_TOP_MID, 0, 50);
    setup_qr_ = lv_qrcode_create(setup_screen_);
    lv_obj_remove_flag(setup_qr_, LV_OBJ_FLAG_CLICKABLE);
    lv_qrcode_set_size(setup_qr_, 320);
    lv_qrcode_set_dark_color(setup_qr_, lv_color_black());
    lv_qrcode_set_light_color(setup_qr_, lv_color_white());
    lv_obj_set_style_pad_all(setup_qr_, 20, 0);
    lv_obj_set_style_bg_color(setup_qr_, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(setup_qr_, LV_OPA_COVER, 0);
    lv_obj_align(setup_qr_, LV_ALIGN_CENTER, 0, -20);
    constexpr char wifi_qr[] = "WIFI:T:nopass;S:p4Radio-Setup;;";
    if (lv_qrcode_update(setup_qr_, wifi_qr, sizeof(wifi_qr) - 1) != LV_RESULT_OK)
        ESP_LOGE(kTag, "Wi-Fi QR code generation failed");
    setup_url_ = lv_label_create(setup_screen_);
    lv_obj_remove_flag(setup_url_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_text_color(setup_url_, lv_color_hex(0xf4f7fb), 0);
    lv_obj_set_width(setup_url_, 640);
    lv_obj_set_style_text_font(setup_url_, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_align(setup_url_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(setup_url_, LV_ALIGN_BOTTOM_MID, 0, -60);

    wake_overlay_ = lv_obj_create(surface_);
    lv_obj_set_size(wake_overlay_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(wake_overlay_, 0, 0);
    lv_obj_set_style_bg_color(wake_overlay_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(wake_overlay_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(wake_overlay_, 0, 0);
    lv_obj_set_style_radius(wake_overlay_, 0, 0);
    lv_obj_remove_flag(wake_overlay_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(wake_overlay_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(wake_overlay_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(wake_overlay_, wake_event, LV_EVENT_CLICKED, this);
    constrain_touch(wake_overlay_);

    // Bar widgets are indicators, never touch targets.
    lv_obj_remove_flag(volume_bar_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(buffer_bar_, LV_OBJ_FLAG_CLICKABLE);
    arrange_widgets();
    lv_obj_update_layout(surface_);
    return true;
}

bool DisplaySkin::activate(lv_obj_t *screen, const DisplaySkin *previous)
{
    // Build each skin once. Retaining the hidden tree avoids allocation churn
    // or dangling callback/pressed-object pointers when users switch repeatedly.
    if (!surface_ && !build(screen)) return false;
    bool wake_visible = g_sleep_requested.load() && !g_resume_requested.load();
    if (previous) {
        manual_settings_qr_ = previous->manual_settings_qr_;
        automatic_qr_dismissed_ = previous->automatic_qr_dismissed_;
        previous_automatic_stage_ = previous->previous_automatic_stage_;
        wake_visible = !lv_obj_has_flag(previous->wake_overlay_, LV_OBJ_FLAG_HIDDEN);
    }
    if (wake_visible) {
        lv_obj_remove_flag(wake_overlay_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(wake_overlay_);
    } else lv_obj_add_flag(wake_overlay_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(surface_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(surface_);
    return true;
}

void DisplaySkin::deactivate()
{
    if (surface_) lv_obj_add_flag(surface_, LV_OBJ_FLAG_HIDDEN);
}

void DisplaySkin::group_label(const char *caption, int y)
{
    lv_obj_t *label = lv_label_create(surface_);
    lv_label_set_text(label, caption);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0xc9d4e2), 0);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, y);
}

void DisplaySkin::constrain_touch(lv_obj_t *object)
{
    lv_obj_add_flag(object, LV_OBJ_FLAG_ADV_HITTEST);
    lv_obj_add_event_cb(object, hit_test_event, LV_EVENT_HIT_TEST, this);
}

void DisplaySkin::hit_test_event(lv_event_t *event)
{
    auto *skin = static_cast<DisplaySkin *>(lv_event_get_user_data(event));
    auto *hit = lv_event_get_hit_test_info(event);
    hit->res = hit->res && skin->contains_point(*hit->point);
}

void DisplaySkin::button_event(lv_event_t *event)
{
    auto *binding = static_cast<ButtonBinding *>(lv_event_get_user_data(event));
    binding->skin->radio_button(binding->action);
}

void DisplaySkin::settings_event(lv_event_t *event)
{
    static_cast<DisplaySkin *>(lv_event_get_user_data(event))->show_settings_qr();
}

void DisplaySkin::dismiss_event(lv_event_t *event)
{
    static_cast<DisplaySkin *>(lv_event_get_user_data(event))->dismiss_setup_qr();
}

void DisplaySkin::wake_event(lv_event_t *event)
{
    static_cast<DisplaySkin *>(lv_event_get_user_data(event))->wake_on_tap();
}

} // namespace p4radio
