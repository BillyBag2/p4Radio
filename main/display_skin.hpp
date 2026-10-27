#pragma once

#include <array>
#include <string>
#include "lvgl.h"

namespace p4radio {

// Each statically allocated skin owns its widgets and callback bindings.
// Shared behavior stays here; derived skins supply geometry and controls.
// All methods run with LVGL's display lock held (including its timer/events).
class DisplaySkin {
public:
    virtual ~DisplaySkin() = default;
    DisplaySkin(const DisplaySkin &) = delete;
    DisplaySkin &operator=(const DisplaySkin &) = delete;
    virtual const char *name() const = 0;
    bool activate(lv_obj_t *screen, const DisplaySkin *previous);
    void deactivate();
    void refresh();

protected:
    DisplaySkin() = default;
    virtual bool prepare_resources() { return true; }
    virtual void build_controls() = 0;
    virtual void arrange_widgets() = 0;
    virtual bool contains_point(const lv_point_t &) const { return true; }

    enum class Action { PreviousStation, NextStation, VolumeDown, VolumeUp, Off, Tune, Count };
    enum class CaptionAlignment { Center, Left, Right };
    lv_obj_t *add_button(const char *caption, int x, int y, int width, int height,
                         Action action, CaptionAlignment alignment = CaptionAlignment::Center);
    void group_label(const char *caption, int y);
    static constexpr int kMeterWidth = 280;
    static constexpr int kMeterHeight = 28;
    static constexpr int kNeedleWidth = 4;
    static constexpr int kMeterMargin = 12;

    lv_obj_t *surface_ = nullptr;
    lv_obj_t *hostname_label_ = nullptr;
    lv_obj_t *station_label_ = nullptr;
    lv_obj_t *status_label_ = nullptr;
    lv_obj_t *power_meter_ = nullptr;
    lv_obj_t *bandwidth_needle_ = nullptr;
    lv_obj_t *volume_bar_ = nullptr;
    lv_obj_t *buffer_bar_ = nullptr;
    lv_obj_t *setup_screen_ = nullptr;
    lv_obj_t *setup_title_ = nullptr;
    lv_obj_t *setup_qr_ = nullptr;
    lv_obj_t *setup_url_ = nullptr;
    lv_obj_t *wake_overlay_ = nullptr;

private:
    bool build(lv_obj_t *screen);
    struct ButtonBinding { DisplaySkin *skin; Action action; };
    std::array<ButtonBinding, static_cast<size_t>(Action::Count)> buttons_{};
    bool manual_settings_qr_ = false;
    bool automatic_qr_dismissed_ = false;
    int previous_automatic_stage_ = 0;
    std::string displayed_qr_;

    void show_settings_qr();
    void dismiss_setup_qr();
    void wake_on_tap();
    void radio_button(Action action);
    void constrain_touch(lv_obj_t *object);
    static void hit_test_event(lv_event_t *event);
    static void button_event(lv_event_t *event);
    static void settings_event(lv_event_t *event);
    static void dismiss_event(lv_event_t *event);
    static void wake_event(lv_event_t *event);
};

DisplaySkin &square_display_skin();
DisplaySkin &circular_display_skin();

} // namespace p4radio
