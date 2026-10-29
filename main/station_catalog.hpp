#pragma once
#include "radio.hpp"
#include "cJSON.h"

namespace p4radio {
// HTTP handlers own/delete returned JSON. Mutations commit before publishing;
// only the settings HTTP task calls the writer, never LVGL or audio callbacks.
cJSON *station_catalog_json();
esp_err_t update_station_catalog(const cJSON *request, std::string &message);
void register_station_http(httpd_handle_t server);
} // namespace p4radio
