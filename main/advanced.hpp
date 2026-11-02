#pragma once

#include "esp_http_server.h"

namespace p4radio {
void register_advanced_http(httpd_handle_t server);
}
