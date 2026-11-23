#include "radio_control.hpp"
#include "radio.hpp"
#include "button_audio.hpp"
#include "cJSON.h"
#include <algorithm>
#include <cstring>
#include <memory>

extern const char control_html[] asm("_binary_control_html_start");

namespace p4radio {

void request_radio_action(RadioAction action)
{
    if (action == RadioAction::Count) return;
    request_button_sound();
    switch (action) {
    case RadioAction::PreviousStation:
    case RadioAction::NextStation:
        select_adjacent_station(action == RadioAction::PreviousStation ? -1 : 1);
        g_playback_requested.store(true);
        request_preferences_save();
        break;
    case RadioAction::VolumeDown:
    case RadioAction::VolumeUp: {
        const int step = action == RadioAction::VolumeDown ? -1 : 1;
        int current = g_volume.load();
        while (!g_volume.compare_exchange_weak(current, std::clamp(current + step, kMinVolume, kMaxVolume))) {}
        request_preferences_save();
        break;
    }
    case RadioAction::Tune:
        g_scan_requested.store(true);
        set_status("Stopping stream for a fresh search");
        break;
    case RadioAction::Off:
        g_resume_requested.store(false);
        g_idle_ready.store(false);
        g_playback_requested.store(false);
        g_sleep_requested.store(true);
        break;
    case RadioAction::Resume:
        if (g_sleep_requested.load()) {
            g_resume_requested.store(true);
            set_status("Resuming radio...");
        }
        break;
    case RadioAction::Count:
        break;
    }
}

namespace {
esp_err_t control_page(httpd_req_t *request)
{
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    return httpd_resp_send(request, control_html, HTTPD_RESP_USE_STRLEN);
}

esp_err_t control_state(httpd_req_t *request)
{
    std::unique_ptr<cJSON, decltype(&cJSON_Delete)> state(cJSON_CreateObject(), cJSON_Delete);
    const bool off = g_sleep_requested.load() && !g_resume_requested.load();
    if (!state ||
        !cJSON_AddStringToObject(state.get(), "station", selected_station_name().c_str()) ||
        !cJSON_AddStringToObject(state.get(), "status", off ? "Radio is off" : status_copy().c_str()) ||
        !cJSON_AddNumberToObject(state.get(), "volume", g_volume.load()) ||
        !cJSON_AddNumberToObject(state.get(), "buffer", g_buffer_percent.load()) ||
        !cJSON_AddNumberToObject(state.get(), "bitrate", g_stream_kbps.load()) ||
        !cJSON_AddBoolToObject(state.get(), "off", off))
        return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
    std::unique_ptr<char, decltype(&cJSON_free)> body(cJSON_PrintUnformatted(state.get()), cJSON_free);
    if (!body) return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_send(request, body.get(), HTTPD_RESP_USE_STRLEN);
}

esp_err_t control_action(httpd_req_t *request)
{
    // A custom header keeps cross-origin forms from changing radio state.
    char command[24]{};
    if (request->content_len != 0 ||
        httpd_req_get_hdr_value_str(request, "X-Radio-Action", command, sizeof(command)) != ESP_OK)
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Missing radio action");

    constexpr struct { const char *name; RadioAction action; } actions[] = {
        {"previous", RadioAction::PreviousStation}, {"next", RadioAction::NextStation},
        {"quieter", RadioAction::VolumeDown}, {"louder", RadioAction::VolumeUp},
        {"tune", RadioAction::Tune}, {"off", RadioAction::Off}, {"resume", RadioAction::Resume},
    };
    for (const auto &entry : actions) {
        if (std::strcmp(command, entry.name) != 0) continue;
        const bool off = g_sleep_requested.load() && !g_resume_requested.load();
        if (off && entry.action != RadioAction::Resume && entry.action != RadioAction::Off) {
            httpd_resp_set_status(request, "409 Conflict");
            return httpd_resp_sendstr(request, "Turn on the radio first");
        }
        if ((entry.action != RadioAction::Off || !off) &&
            (entry.action != RadioAction::Resume || off))
            request_radio_action(entry.action);
        return control_state(request);
    }
    return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Unknown radio action");
}
}

void register_control_http(httpd_handle_t server)
{
    const httpd_uri_t routes[] = {
        {.uri = "/control", .method = HTTP_GET, .handler = control_page, .user_ctx = nullptr},
        {.uri = "/api/control", .method = HTTP_GET, .handler = control_state, .user_ctx = nullptr},
        {.uri = "/api/control", .method = HTTP_POST, .handler = control_action, .user_ctx = nullptr},
    };
    for (const auto &route : routes) ESP_ERROR_CHECK(httpd_register_uri_handler(server, &route));
}
}
