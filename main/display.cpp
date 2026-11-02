#include "radio.hpp"
#include "display_skin.hpp"
#include <algorithm>
#include "esp_log.h"
#include "esp_timer.h"

namespace p4radio {
namespace {

DisplaySkin *active_skin = nullptr;
DisplaySkinId last_requested_skin = DisplaySkinId::Square;
bool attempted_skin = false;
int64_t render_started = 0;
int64_t render_us = 0;
int64_t render_max_us = 0;
int64_t update_us = 0;
int64_t update_max_us = 0;
unsigned rendered_frames = 0;
unsigned updates = 0;
int64_t timing_window_started = 0;

void record_render(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_RENDER_START) render_started = esp_timer_get_time();
    else if (render_started) {
        const int64_t elapsed = esp_timer_get_time() - render_started;
        render_us += elapsed;
        render_max_us = std::max(render_max_us, elapsed);
        ++rendered_frames;
        render_started = 0;
    }
}

void log_gui_timing(lv_timer_t *)
{
    const int64_t now = esp_timer_get_time();
    const int64_t elapsed = now - timing_window_started;
    ESP_LOGI(kTag, "GUI: %.1f rendered fps, render avg/max %.2f/%.2f ms, "
             "update avg/max %.2f/%.2f ms, render wall %.1f%%",
             elapsed > 0 ? rendered_frames * 1000000.0 / elapsed : 0.0,
             rendered_frames ? render_us / (1000.0 * rendered_frames) : 0.0, render_max_us / 1000.0,
             updates ? update_us / (1000.0 * updates) : 0.0, update_max_us / 1000.0,
             elapsed > 0 ? render_us * 100.0 / elapsed : 0.0);
    render_us = render_max_us = update_us = update_max_us = 0;
    rendered_frames = updates = 0;
    timing_window_started = now;
}

// Called only in the LVGL context or while holding the BSP display lock.
void refresh_display(lv_timer_t *)
{
    const int64_t started = esp_timer_get_time();
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
    const int64_t elapsed = esp_timer_get_time() - started;
    update_us += elapsed;
    update_max_us = std::max(update_max_us, elapsed);
    ++updates;
}

} // namespace

void build_ui()
{
    if (!board_display_start()) {
        ESP_LOGE(kTag, "Board display initialization failed");
        return;
    }
    board_display_brightness_set(100);
    if (!board_display_lock(-1)) return;
    lv_display_t *display = lv_display_get_default();
    lv_display_add_event_cb(display, record_render, LV_EVENT_RENDER_START, nullptr);
    lv_display_add_event_cb(display, record_render, LV_EVENT_RENDER_READY, nullptr);
    timing_window_started = esp_timer_get_time();
    refresh_display(nullptr);
    lv_timer_create(refresh_display, 500, nullptr);
    lv_timer_create(log_gui_timing, 10000, nullptr);
    board_display_unlock();
}

} // namespace p4radio
