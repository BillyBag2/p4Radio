#include "radio.hpp"
#include "display_skin.hpp"
#include "esp_log.h"

namespace p4radio {
namespace {

DisplaySkin *active_skin = nullptr;
DisplaySkinId last_requested_skin = DisplaySkinId::Square;
bool attempted_skin = false;

// Called only in the LVGL context or while holding the BSP display lock.
void refresh_display(lv_timer_t *)
{
    const DisplaySkinId requested = g_display_skin.load();
    if (!attempted_skin || requested != last_requested_skin) {
        attempted_skin = true;
        last_requested_skin = requested;
        DisplaySkin &next = requested == DisplaySkinId::Retro ? retro_display_skin() :
            requested == DisplaySkinId::Circular ? circular_display_skin() : square_display_skin();
        if (&next != active_skin) {
            if (next.activate(lv_screen_active(), active_skin)) {
                if (active_skin) {
                    active_skin->deactivate();
                    // Consume any touch spanning the layout change.
                    for (lv_indev_t *input = lv_indev_get_next(nullptr); input; input = lv_indev_get_next(input))
                        lv_indev_wait_release(input);
                }
                active_skin = &next;
                ESP_LOGI(kTag, "Display skin: %s", next.name());
            } else {
                ESP_LOGE(kTag, "Cannot activate display skin: %s", next.name());
                // Keep the previous screen usable. On boot, fall back to the
                // square layout if circular drawing memory is unavailable.
                if (!active_skin && square_display_skin().activate(lv_screen_active(), nullptr))
                    active_skin = &square_display_skin();
            }
        }
    }
    if (active_skin) active_skin->refresh();
}

} // namespace

void build_ui()
{
    if (!bsp_display_start()) {
        ESP_LOGE(kTag, "Board display initialization failed");
        return;
    }
    bsp_display_brightness_set(100);
    if (!bsp_display_lock(-1)) return;
    refresh_display(nullptr);
    lv_timer_create(refresh_display, 500, nullptr);
    bsp_display_unlock();
}

} // namespace p4radio
