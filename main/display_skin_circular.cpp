#include "display_skin.hpp"
#include "esp_heap_caps.h"
#include "esp_log.h"

namespace p4radio {
namespace {

class CircularSkin final : public DisplaySkin {
public:
    const char *name() const override { return "Circular"; }

private:
    bool prepare_resources() override
    {
#if LV_USE_STDLIB_MALLOC == LV_STDLIB_BUILTIN
        // Rounded child clipping can create two 720 x 360 ARGB layers.
        // The board's default 64 KB LVGL heap cannot hold these. Keep this
        // extra pool in PSRAM for the entire lifetime of the static skin;
        // never release it while LVGL widgets/draw tasks can reference it.
        if (pool_memory_) return true;
        constexpr size_t pool_bytes = 3 * 1024 * 1024;
        // LVGL 9.3 derives TLSF's maximum block size from these settings.
        // Allocating PSRAM alone is insufficient: a default 64 KB TLSF
        // instance rejects a larger pool even when plenty of RAM is free.
        static_assert(LV_MEM_SIZE + LV_MEM_POOL_EXPAND_SIZE >= pool_bytes,
                      "Circular skin requires CONFIG_LV_MEM_POOL_EXPAND_SIZE_KILOBYTES=3072; "
                      "update the active sdkconfig as well as sdkconfig.defaults");
        pool_memory_ = heap_caps_malloc(pool_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!pool_memory_) {
            ESP_LOGE("display_skin", "Cannot allocate circular skin PSRAM pool");
            return false;
        }
        if (!lv_mem_add_pool(pool_memory_, pool_bytes)) {
            heap_caps_free(pool_memory_);
            pool_memory_ = nullptr;
            ESP_LOGE("display_skin", "Cannot register circular skin LVGL pool");
            return false;
        }
#endif
        return true;
    }

    void *pool_memory_ = nullptr;

    void build_controls() override
    {
        // Equal-sized controls fit fully inside the circle, including the
        // lower station row where the curved boundary narrows the space.
        constexpr int button_width = 236;
        constexpr int button_height = 100;
        constexpr int left = 84;
        constexpr int right = 400;
        group_label("VOLUME", 286);
        add_button("-", left, 322, button_width, button_height, Action::VolumeDown);
        add_button("+", right, 322, button_width, button_height, Action::VolumeUp);
        group_label("STATION", 438);
        add_button("<", left, 474, button_width, button_height, Action::PreviousStation);
        add_button(">", right, 474, button_width, button_height, Action::NextStation);
        add_button("OFF", 174, 598, 174, 70, Action::Off);
        add_button("TUNE", 372, 598, 174, 70, Action::Tune);
    }

    void arrange_widgets() override
    {
        // Clip all descendants, including QR and wake overlays. The outer
        // active screen remains black when testing on the square panel.
        lv_obj_set_style_radius(surface_, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_clip_corner(surface_, true, 0);

        lv_obj_set_width(hostname_label_, 380);
        lv_obj_align(hostname_label_, LV_ALIGN_TOP_MID, 0, 64);
        lv_obj_set_width(station_label_, 540);
        lv_obj_align(station_label_, LV_ALIGN_TOP_MID, 0, 126);
        lv_obj_set_width(status_label_, 570);
        lv_obj_set_height(status_label_, 58);
        lv_label_set_long_mode(status_label_, LV_LABEL_LONG_DOT);
        lv_obj_align(status_label_, LV_ALIGN_TOP_MID, 0, 198);
        lv_obj_align(power_meter_, LV_ALIGN_TOP_MID, 0, 256);

        // Full-height edge bars would disappear around the curved boundary.
        lv_obj_set_size(buffer_bar_, 8, 140);
        lv_obj_align(buffer_bar_, LV_ALIGN_LEFT_MID, 26, 0);
        lv_obj_set_size(volume_bar_, 8, 140);
        lv_obj_align(volume_bar_, LV_ALIGN_RIGHT_MID, -26, 0);
        lv_obj_move_foreground(buffer_bar_);
        lv_obj_move_foreground(volume_bar_);
        lv_obj_move_foreground(setup_screen_);
        lv_obj_move_foreground(wake_overlay_);

        lv_obj_set_width(setup_title_, 440);
        lv_obj_align(setup_title_, LV_ALIGN_TOP_MID, 0, 80);
        lv_obj_set_width(setup_url_, 400);
        lv_obj_align(setup_url_, LV_ALIGN_BOTTOM_MID, 0, -70);
    }

    bool contains_point(const lv_point_t &point) const override
    {
        lv_area_t bounds;
        lv_obj_get_coords(surface_, &bounds);
        // Doubled coordinates keep the half-pixel center exact (359.5).
        const int dx = 2 * point.x - bounds.x1 - bounds.x2;
        const int dy = 2 * point.y - bounds.y1 - bounds.y2;
        return dx * dx + dy * dy <= 720 * 720;
    }
};

} // namespace

DisplaySkin &circular_display_skin()
{
    static CircularSkin skin;
    return skin;
}

} // namespace p4radio
