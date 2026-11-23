#pragma once
#include "esp_http_server.h"

namespace p4radio {
enum class RadioAction { PreviousStation, NextStation, VolumeDown, VolumeUp, Off, Tune, Count, Resume };
void request_radio_action(RadioAction action);
void register_control_http(httpd_handle_t server);
}
