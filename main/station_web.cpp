#include "station_catalog.hpp"
#include "radio_browser_dns.hpp"
#include "station_rules.hpp"
#include <cstring>
#include <memory>
#include "esp_timer.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

extern const char stations_html[] asm("_binary_stations_html_start");
extern const char stations_js[] asm("_binary_stations_js_start");
extern const char stations_css[] asm("_binary_stations_css_start");

namespace p4radio {
namespace {
using Json = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>;
StaticSemaphore_t directory_mutex_state;
SemaphoreHandle_t directory_mutex;
std::vector<std::string> directory_servers;
std::atomic<bool> directory_busy{false};
std::atomic<int64_t> directory_attempt{0};
std::atomic<int64_t> directory_success{0};

esp_err_t send_json(httpd_req_t *request, cJSON *value)
{
    Json owned(value, cJSON_Delete);
    if (!value) return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
    std::unique_ptr<char, decltype(&cJSON_free)> body(cJSON_PrintUnformatted(value), cJSON_free);
    if (!body) return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_send(request, body.get(), HTTPD_RESP_USE_STRLEN);
}

esp_err_t asset(httpd_req_t *request)
{
    const char *path = static_cast<const char *>(request->user_ctx);
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    httpd_resp_set_type(request, path == stations_js ? "text/javascript; charset=utf-8" :
        path == stations_css ? "text/css; charset=utf-8" : "text/html; charset=utf-8");
    return httpd_resp_send(request, path, HTTPD_RESP_USE_STRLEN);
}

esp_err_t list_stations(httpd_req_t *request) { return send_json(request, station_catalog_json()); }

esp_err_t change_stations(httpd_req_t *request)
{
    char content_type[64]{};
    if (httpd_req_get_hdr_value_str(request, "Content-Type", content_type, sizeof(content_type)) != ESP_OK ||
        (std::strcmp(content_type, "application/json") && std::strncmp(content_type, "application/json;", 17)))
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Send an application/json request");
    if (request->content_len <= 0 || request->content_len > 4096)
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Invalid station request size");
    std::string body(request->content_len, '\0');
    size_t received = 0;
    while (received < body.size()) {
        const int count = httpd_req_recv(request, body.data() + received, body.size() - received);
        if (count <= 0) return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Incomplete station request");
        received += count;
    }
    // Bound recursion before entering cJSON on the 6 KB HTTP task stack.
    if (!station_rules::bounded_json(body))
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Invalid station JSON");
    const char *end = nullptr;
    Json command(cJSON_ParseWithLengthOpts(body.c_str(), body.size() + 1, &end, true), cJSON_Delete);
    if (end != body.c_str() + body.size())
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Invalid station JSON");
    std::string message;
    const esp_err_t result = cJSON_IsObject(command.get()) ? update_station_catalog(command.get(), message) : ESP_ERR_INVALID_ARG;
    if (message.empty()) message = result == ESP_ERR_NO_MEM ? "Station storage is full" : "Invalid station request";
    cJSON *reply = cJSON_CreateObject();
    if (reply) {
        cJSON_AddBoolToObject(reply, "ok", result == ESP_OK);
        cJSON_AddStringToObject(reply, "message", message.c_str());
    }
    if (result != ESP_OK) httpd_resp_set_status(request, result == ESP_ERR_INVALID_STATE ? "409 Conflict" :
        result == ESP_ERR_INVALID_ARG ? "400 Bad Request" : "507 Insufficient Storage");
    return send_json(request, reply);
}

void discover_directory(void *)
{
    {
        auto servers = discover_radio_browser_servers(false); // Works during OFF/idle too.
        xSemaphoreTake(directory_mutex, portMAX_DELAY);
        directory_servers = std::move(servers);
        const bool success = !directory_servers.empty();
        xSemaphoreGive(directory_mutex);
        directory_success.store(success ? esp_timer_get_time() : 0);
    } // Destroy C++ locals before the task deletes itself.
    directory_busy.store(false);
    vTaskDelete(nullptr);
}

esp_err_t list_directory(httpd_req_t *request)
{
    char query[64]{}, refresh[4]{};
    const bool force = httpd_req_get_url_query_str(request, query, sizeof(query)) == ESP_OK &&
        httpd_query_key_value(query, "refresh", refresh, sizeof(refresh)) == ESP_OK && !std::strcmp(refresh, "1");
    const int64_t now = esp_timer_get_time(), attempted = directory_attempt.load(), success = directory_success.load();
    if (g_connected.load() && (force || !success || now - success > 900000000) &&
        (!attempted || now - attempted > 30000000) && !directory_busy.exchange(true)) {
        directory_attempt.store(now);
        if (xTaskCreate(discover_directory, "directory_dns", 6144, nullptr, 3, nullptr) != pdPASS)
            directory_busy.store(false);
    }
    cJSON *root = cJSON_CreateObject();
    if (!root) return send_json(request, nullptr);
    cJSON_AddBoolToObject(root, "connected", g_connected.load());
    cJSON_AddBoolToObject(root, "busy", directory_busy.load());
    cJSON *servers = cJSON_AddArrayToObject(root, "servers");
    xSemaphoreTake(directory_mutex, portMAX_DELAY);
    if (servers) for (const auto &server : directory_servers) cJSON_AddItemToArray(servers, cJSON_CreateString(server.c_str()));
    xSemaphoreGive(directory_mutex);
    return send_json(request, root);
}
} // namespace

void register_station_http(httpd_handle_t server)
{
    directory_mutex = xSemaphoreCreateMutexStatic(&directory_mutex_state);
    const httpd_uri_t page{.uri = "/stations", .method = HTTP_GET, .handler = asset, .user_ctx = const_cast<char *>(stations_html)};
    const httpd_uri_t js{.uri = "/stations.js", .method = HTTP_GET, .handler = asset, .user_ctx = const_cast<char *>(stations_js)};
    const httpd_uri_t css{.uri = "/stations.css", .method = HTTP_GET, .handler = asset, .user_ctx = const_cast<char *>(stations_css)};
    const httpd_uri_t list{.uri = "/api/stations", .method = HTTP_GET, .handler = list_stations, .user_ctx = nullptr};
    const httpd_uri_t update{.uri = "/api/stations", .method = HTTP_POST, .handler = change_stations, .user_ctx = nullptr};
    const httpd_uri_t directory{.uri = "/api/directory", .method = HTTP_GET, .handler = list_directory, .user_ctx = nullptr};
    for (const auto *route : {&page, &js, &css, &list, &update, &directory})
        ESP_ERROR_CHECK(httpd_register_uri_handler(server, route));
}
} // namespace p4radio
