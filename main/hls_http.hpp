#pragma once
#include <cstddef>
#include <string>
#include "esp_http_client.h"

namespace p4radio {
// One playback-task-owned client. perform() consumes complete responses so the
// transport can survive between GETs; retunes/errors destroy it with the session.
class HlsHttpSession {
public:
    HlsHttpSession() = default;
    ~HlsHttpSession();
    HlsHttpSession(const HlsHttpSession &) = delete;
    HlsHttpSession &operator=(const HlsHttpSession &) = delete;
    bool get(const char *purpose, const std::string &url, void *destination, size_t capacity, size_t &size);
private:
    static esp_err_t event(esp_http_client_event_t *event);
    esp_http_client_handle_t client_ = nullptr;
    char *destination_ = nullptr;
    size_t capacity_ = 0;
    size_t size_ = 0;
    unsigned connections_ = 0;
    bool overflow_ = false;
};
}
