#pragma once

#include <cstdint>
#include "esp_err.h"
#include "esp_codec_dev.h"
#include "lvgl.h"

namespace p4radio {

// Hardware operations implemented only by the selected board. Callers retain
// the existing LVGL and speaker locking/lifecycle responsibilities.
void board_init();
esp_codec_dev_handle_t board_speaker_init();
bool board_display_start();
bool board_display_is_round();
bool board_display_lock(int32_t timeout_ms);
void board_display_unlock();
esp_err_t board_display_brightness_set(int percent);
esp_err_t board_display_backlight_off();

} // namespace p4radio
