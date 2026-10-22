#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "bsp/esp32_p4_wifi6_touch_lcd_4b.h"
#include "driver/gpio.h"
#include "esp_audio_dec_default.h"
#include "esp_audio_simple_dec.h"
#include "esp_audio_simple_dec_default.h"
#include "esp_crt_bundle.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_sleep.h"
#include "esp_wifi.h"
#include "esp_wifi_remote.h"
#include "esp_hosted.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "nvs.h"
#include "nvs_flash.h"

namespace {

constexpr char kTag[] = "p4radio";
constexpr char kApSsid[] = "p4Radio-Setup";
constexpr int kStationCount = 5;
constexpr int kMaxVolume = 100;
constexpr int kMinVolume = 0;
constexpr int kDnsPort = 53;
constexpr int kHttpPort = 80;
constexpr size_t kHlsSegmentMaxSize = 512 * 1024;
constexpr size_t kMaxHlsSegments = 24;

struct Station {
    const char *name;
    const char *playlist_url;
};

// BBC Radio 4 Extra occupies the repeated BBC4 slot in the original station list.
constexpr std::array<Station, kStationCount> kStations{{
    {"BBC Radio 1", "https://as-hls-uk-live.akamaized.net/pool_01505109/live/uk/bbc_radio_one/bbc_radio_one.isml/bbc_radio_one-audio=320000.norewind.m3u8"},
    {"BBC Radio 2", "https://as-hls-uk-live.akamaized.net/pool_74208725/live/uk/bbc_radio_two/bbc_radio_two.isml/bbc_radio_two-audio=320000.norewind.m3u8"},
    {"BBC Radio 4", "https://as-hls-uk-live.akamaized.net/pool_55057080/live/uk/bbc_radio_fourfm/bbc_radio_fourfm.isml/bbc_radio_fourfm-audio=320000.norewind.m3u8"},
    {"BBC Radio 4 Extra", "https://as-hls-uk-live.akamaized.net/pool_26173715/live/uk/bbc_radio_four_extra/bbc_radio_four_extra.isml/bbc_radio_four_extra-audio=320000.norewind.m3u8"},
    {"BBC Radio 5 Live", "https://as-hls-uk-live.akamaized.net/pool_89021708/live/uk/bbc_radio_five_live/bbc_radio_five_live.isml/bbc_radio_five_live-audio=320000.norewind.m3u8"},
}};

std::atomic<int> g_station{0};
std::atomic<int> g_volume{50};
std::atomic<bool> g_connected{false};
std::atomic<bool> g_internet_probe_pending{false};
std::atomic<bool> g_portal_active{false};
std::atomic<bool> g_playback_requested{true};
std::atomic<bool> g_sleep_requested{false};
std::atomic<int> g_station_retries{0};
httpd_handle_t g_http_server = nullptr;
esp_codec_dev_handle_t g_speaker = nullptr;
lv_obj_t *g_station_label = nullptr;
lv_obj_t *g_status_label = nullptr;
lv_obj_t *g_volume_label = nullptr;
portMUX_TYPE g_status_mux = portMUX_INITIALIZER_UNLOCKED;
char g_status[96] = "Starting radio";
char g_saved_ssid[33] = {};
char g_saved_password[65] = {};

void set_status(const char *message)
{
    portENTER_CRITICAL(&g_status_mux);
    std::snprintf(g_status, sizeof(g_status), "%s", message);
    portEXIT_CRITICAL(&g_status_mux);
    ESP_LOGI(kTag, "%s", message);
}

std::string status_copy()
{
    char copy[sizeof(g_status)];
    portENTER_CRITICAL(&g_status_mux);
    std::memcpy(copy, g_status, sizeof(copy));
    portEXIT_CRITICAL(&g_status_mux);
    copy[sizeof(copy) - 1] = '\0';
    return copy;
}

void save_preferences()
{
    nvs_handle_t nvs;
    if (nvs_open("p4radio", NVS_READWRITE, &nvs) != ESP_OK) {
        return;
    }
    nvs_set_i32(nvs, "station", g_station.load());
    nvs_set_i32(nvs, "volume", g_volume.load());
    nvs_commit(nvs);
    nvs_close(nvs);
}

void load_preferences()
{
    nvs_handle_t nvs;
    if (nvs_open("p4radio", NVS_READONLY, &nvs) != ESP_OK) {
        return;
    }
    int32_t value = 0;
    if (nvs_get_i32(nvs, "station", &value) == ESP_OK && value >= 0 && value < kStationCount) {
        g_station.store(value);
    }
    if (nvs_get_i32(nvs, "volume", &value) == ESP_OK) {
        g_volume.store(std::clamp<int>(value, kMinVolume, kMaxVolume));
    }
    size_t size = sizeof(g_saved_ssid);
    nvs_get_str(nvs, "ssid", g_saved_ssid, &size);
    size = sizeof(g_saved_password);
    nvs_get_str(nvs, "password", g_saved_password, &size);
    nvs_close(nvs);
}

void save_wifi_credentials(const char *ssid, const char *password)
{
    nvs_handle_t nvs;
    ESP_ERROR_CHECK(nvs_open("p4radio", NVS_READWRITE, &nvs));
    ESP_ERROR_CHECK(nvs_set_str(nvs, "ssid", ssid));
    ESP_ERROR_CHECK(nvs_set_str(nvs, "password", password));
    ESP_ERROR_CHECK(nvs_commit(nvs));
    nvs_close(nvs);
    std::snprintf(g_saved_ssid, sizeof(g_saved_ssid), "%s", ssid);
    std::snprintf(g_saved_password, sizeof(g_saved_password), "%s", password);
}

std::string url_decode(const char *input)
{
    std::string output;
    for (size_t i = 0; input[i] != '\0'; ++i) {
        if (input[i] == '+' ) {
            output.push_back(' ');
        } else if (input[i] == '%' && std::isxdigit(static_cast<unsigned char>(input[i + 1])) &&
                   std::isxdigit(static_cast<unsigned char>(input[i + 2]))) {
            char hex[3] = {input[i + 1], input[i + 2], 0};
            output.push_back(static_cast<char>(std::strtol(hex, nullptr, 16)));
            i += 2;
        } else {
            output.push_back(input[i]);
        }
    }
    return output;
}

std::string form_value(const std::string &body, const char *wanted)
{
    size_t start = 0;
    while (start < body.size()) {
        const size_t end = body.find('&', start);
        const std::string field = body.substr(start, end == std::string::npos ? end : end - start);
        const size_t equals = field.find('=');
        if (equals != std::string::npos && field.compare(0, equals, wanted) == 0) {
            return url_decode(field.c_str() + equals + 1);
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    return {};
}

void dns_task(void *)
{
    const int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fd < 0) {
        vTaskDelete(nullptr);
        return;
    }
    timeval timeout{1, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_port = htons(kDnsPort);
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(fd, reinterpret_cast<sockaddr *>(&local), sizeof(local)) != 0) {
        close(fd);
        vTaskDelete(nullptr);
        return;
    }

    uint8_t packet[512];
    while (true) {
        sockaddr_in peer{};
        socklen_t peer_len = sizeof(peer);
        const int length = recvfrom(fd, packet, sizeof(packet), 0, reinterpret_cast<sockaddr *>(&peer), &peer_len);
        if (length < 17 || length > 490 || !g_portal_active.load()) {
            continue;
        }
        // Answer every DNS A query with the captive portal address.
        packet[2] = 0x81;
        packet[3] = 0x80;
        packet[6] = 0;
        packet[7] = 1;
        packet[8] = packet[9] = packet[10] = packet[11] = 0;
        size_t offset = static_cast<size_t>(length);
        packet[offset++] = 0xC0;
        packet[offset++] = 0x0C;
        packet[offset++] = 0;
        packet[offset++] = 1;
        packet[offset++] = 0;
        packet[offset++] = 1;
        packet[offset++] = 0;
        packet[offset++] = 0;
        packet[offset++] = 0;
        packet[offset++] = 30;
        packet[offset++] = 0;
        packet[offset++] = 4;
        packet[offset++] = 192;
        packet[offset++] = 168;
        packet[offset++] = 4;
        packet[offset++] = 1;
        sendto(fd, packet, offset, 0, reinterpret_cast<sockaddr *>(&peer), peer_len);
    }
}

esp_err_t portal_page(httpd_req_t *request)
{
    static constexpr char page[] = R"HTML(<!doctype html><html><head><meta name="viewport" content="width=device-width,initial-scale=1"><title>p4Radio Wi-Fi setup</title><style>
body{font:17px system-ui;background:#101827;color:#f3f6fb;margin:0;padding:24px}main{max-width:480px;margin:auto;background:#1b2638;padding:24px;border-radius:18px}h1{margin-top:0}label{display:block;margin:16px 0 6px}input,select,button{box-sizing:border-box;width:100%;padding:13px;border:0;border-radius:9px;font:inherit}button{background:#60d6ad;font-weight:700;margin-top:18px}small{color:#a8b7ca}</style></head><body><main><h1>Connect p4Radio</h1><p>Choose your Wi-Fi network and enter its password. Your details stay on this radio.</p><form method="post" action="/connect"><label for="ssid">Wi-Fi network</label><select id="networks" onchange="document.getElementById('ssid').value=this.value"><option value="">Scanning…</option></select><input id="ssid" name="ssid" maxlength="32" placeholder="Network name" required><label for="password">Password</label><input id="password" name="password" type="password" maxlength="64" autocomplete="current-password"><button>Save and connect</button></form><p><small>If the list is empty, type the network name. This open setup network does not provide internet.</small></p></main><script>fetch('/api/scan').then(r=>r.json()).then(a=>{let s=document.getElementById('networks');s.innerHTML='<option value="">Select a network…</option>';a.forEach(n=>{let o=document.createElement('option');o.value=n;o.textContent=n;s.appendChild(o)})}).catch(()=>{});</script></body></html>)HTML";
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_send(request, page, HTTPD_RESP_USE_STRLEN);
}

esp_err_t scan_networks(httpd_req_t *request)
{
    wifi_scan_config_t scan{};
    esp_err_t err = esp_wifi_scan_start(&scan, true);
    if (err != ESP_OK && err != ESP_ERR_WIFI_STATE) {
        return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Wi-Fi scan failed");
    }
    uint16_t count = 0;
    if (esp_wifi_scan_get_ap_num(&count) != ESP_OK) {
        count = 0;
    }
    count = std::min<uint16_t>(count, 24);
    std::array<wifi_ap_record_t, 24> records{};
    if (count && esp_wifi_scan_get_ap_records(&count, records.data()) != ESP_OK) {
        count = 0;
    }
    std::string response = "[";
    bool first = true;
    for (uint16_t i = 0; i < count; ++i) {
        auto *ssid_data = reinterpret_cast<char *>(records[i].ssid);
        std::string ssid(ssid_data, strnlen(ssid_data, sizeof(records[i].ssid)));
        if (ssid.empty()) {
            continue;
        }
        if (!first) response += ",";
        first = false;
        response += "\"";
        for (char ch : ssid) {
            if (ch == '\\' || ch == '"') response += '\\';
            if (static_cast<unsigned char>(ch) >= 0x20) response += ch;
        }
        response += "\"";
    }
    response += "]";
    httpd_resp_set_type(request, "application/json");
    return httpd_resp_send(request, response.c_str(), response.size());
}

void configure_station(const char *ssid, const char *password)
{
    wifi_config_t config{};
    std::snprintf(reinterpret_cast<char *>(config.sta.ssid), sizeof(config.sta.ssid), "%s", ssid);
    std::snprintf(reinterpret_cast<char *>(config.sta.password), sizeof(config.sta.password), "%s", password);
    config.sta.threshold.authmode = WIFI_AUTH_OPEN;
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &config));
    g_connected.store(false);
    g_station_retries.store(0);
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    esp_wifi_connect();
    set_status("Connecting to Wi-Fi…");
}

esp_err_t connect_network(httpd_req_t *request)
{
    if (request->content_len <= 0 || request->content_len > 256) {
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Invalid form");
    }
    std::string body(static_cast<size_t>(request->content_len), '\0');
    size_t received = 0;
    while (received < body.size()) {
        const int count = httpd_req_recv(request, body.data() + received, body.size() - received);
        if (count <= 0) {
            return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Incomplete form");
        }
        received += count;
    }
    const std::string ssid = form_value(body, "ssid");
    const std::string password = form_value(body, "password");
    if (ssid.empty() || ssid.size() > 32 || password.size() > 64) {
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Invalid network name or password");
    }
    save_wifi_credentials(ssid.c_str(), password.c_str());
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    static constexpr char response[] = "<!doctype html><meta name=viewport content='width=device-width'><h2>Connecting…</h2><p>Keep p4Radio powered on. The setup network will close after it joins Wi-Fi.</p>";
    const esp_err_t result = httpd_resp_send(request, response, HTTPD_RESP_USE_STRLEN);
    configure_station(ssid.c_str(), password.c_str());
    return result;
}

esp_err_t captive_redirect(httpd_req_t *request)
{
    httpd_resp_set_status(request, "302 Found");
    httpd_resp_set_hdr(request, "Location", "http://192.168.4.1/");
    return httpd_resp_send(request, nullptr, 0);
}

void start_portal()
{
    if (g_portal_active.exchange(true)) {
        return;
    }
    wifi_config_t ap{};
    std::snprintf(reinterpret_cast<char *>(ap.ap.ssid), sizeof(ap.ap.ssid), "%s", kApSsid);
    ap.ap.ssid_len = std::strlen(kApSsid);
    ap.ap.channel = 6;
    ap.ap.max_connection = 4;
    ap.ap.authmode = WIFI_AUTH_OPEN;
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = kHttpPort;
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.stack_size = 6144;
    ESP_ERROR_CHECK(httpd_start(&g_http_server, &config));
    const httpd_uri_t root{.uri = "/", .method = HTTP_GET, .handler = portal_page, .user_ctx = nullptr};
    const httpd_uri_t scan{.uri = "/api/scan", .method = HTTP_GET, .handler = scan_networks, .user_ctx = nullptr};
    const httpd_uri_t connect{.uri = "/connect", .method = HTTP_POST, .handler = connect_network, .user_ctx = nullptr};
    const httpd_uri_t redirect{.uri = "/*", .method = HTTP_GET, .handler = captive_redirect, .user_ctx = nullptr};
    ESP_ERROR_CHECK(httpd_register_uri_handler(g_http_server, &root));
    ESP_ERROR_CHECK(httpd_register_uri_handler(g_http_server, &scan));
    ESP_ERROR_CHECK(httpd_register_uri_handler(g_http_server, &connect));
    ESP_ERROR_CHECK(httpd_register_uri_handler(g_http_server, &redirect));
    xTaskCreate(dns_task, "captive_dns", 3072, nullptr, 3, nullptr);
    set_status("Setup Wi-Fi: join p4Radio-Setup");
}

void stop_portal()
{
    g_portal_active.store(false);
    if (g_http_server) {
        httpd_stop(g_http_server);
        g_http_server = nullptr;
    }
    esp_wifi_set_mode(WIFI_MODE_STA);
}

void wifi_event_handler(void *, esp_event_base_t base, int32_t id, void *)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        if (g_saved_ssid[0]) {
            esp_wifi_connect();
        }
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        g_connected.store(false);
        if (g_saved_ssid[0] && g_station_retries.fetch_add(1) < 5) {
            vTaskDelay(pdMS_TO_TICKS(400));
            esp_wifi_connect();
        } else if (!g_portal_active.load()) {
            start_portal();
        } else {
            set_status("Wi-Fi join failed; retry in setup page");
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        g_connected.store(true);
        g_internet_probe_pending.store(true);
        g_station_retries.store(0);
        if (g_portal_active.load()) {
            stop_portal();
        }
        char message[96];
        std::snprintf(message, sizeof(message), "Connected to %.30s", g_saved_ssid);
        set_status(message);
    }
}

bool ensure_network_stack()
{
    const int hosted_init = esp_hosted_init();
    const int hosted_link = hosted_init == 0 ? esp_hosted_connect_to_slave() : hosted_init;
    if (hosted_link != 0) {
        set_status("C6 Wi-Fi link unavailable; check its ESP-Hosted firmware");
        ESP_LOGE(kTag, "ESP-Hosted init/link failed: %d/%d", hosted_init, hosted_link);
        return false;
    }
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();
    wifi_init_config_t wifi_config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_config));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, nullptr));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, nullptr));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(g_saved_ssid[0] ? WIFI_MODE_STA : WIFI_MODE_APSTA));
    if (g_saved_ssid[0]) {
        wifi_config_t station{};
        std::strncpy(reinterpret_cast<char *>(station.sta.ssid), g_saved_ssid, sizeof(station.sta.ssid) - 1);
        station.sta.ssid[sizeof(station.sta.ssid) - 1] = '\0';
        std::strncpy(reinterpret_cast<char *>(station.sta.password), g_saved_password, sizeof(station.sta.password) - 1);
        station.sta.password[sizeof(station.sta.password) - 1] = '\0';
        station.sta.threshold.authmode = WIFI_AUTH_OPEN;
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &station));
    }
    ESP_ERROR_CHECK(esp_wifi_start());

    if (g_saved_ssid[0]) {
        set_status("Connecting to saved Wi-Fi…");
    } else {
        start_portal();
    }
    return true;
}

std::string read_http_body(esp_http_client_handle_t client, int max_len)
{
    std::string body;
    std::array<char, 512> chunk{};
    while (static_cast<int>(body.size()) < max_len) {
        const int count = esp_http_client_read(client, chunk.data(), std::min<int>(chunk.size(), max_len - body.size()));
        if (count <= 0) break;
        body.append(chunk.data(), count);
    }
    return body;
}

std::string resolve_station_url(const Station &station)
{
    ESP_LOGI(kTag, "Using BBC HLS playlist for %s", station.name);
    return station.playlist_url;
}

esp_http_client_handle_t open_http_get(const std::string &url, int &status_code, esp_err_t &error)
{
    esp_http_client_config_t config{};
    config.url = url.c_str();
    config.timeout_ms = 15000;
    config.buffer_size = 8192;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.user_agent = "p4Radio/1.0 ESP-IDF";
    config.disable_auto_redirect = false;
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        error = ESP_ERR_NO_MEM;
        return nullptr;
    }
    error = esp_http_client_open(client, 0);
    if (error != ESP_OK) {
        esp_http_client_cleanup(client);
        return nullptr;
    }
    esp_http_client_fetch_headers(client);
    status_code = esp_http_client_get_status_code(client);
    if (status_code < 200 || status_code >= 300) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        error = ESP_FAIL;
        return nullptr;
    }
    return client;
}

std::string fetch_hls_playlist(const std::string &url)
{
    int status_code = 0;
    esp_err_t error = ESP_OK;
    esp_http_client_handle_t client = open_http_get(url, status_code, error);
    if (!client) {
        ESP_LOGE(kTag, "HLS playlist request failed: %s (HTTP %d)", esp_err_to_name(error), status_code);
        return {};
    }
    std::string body = read_http_body(client, 16384);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    if (body.find("#EXTM3U") == std::string::npos) {
        ESP_LOGE(kTag, "HLS endpoint returned an invalid playlist (HTTP %d)", status_code);
        return {};
    }
    return body;
}

std::string make_hls_segment_url(const std::string &playlist_url, const std::string &segment)
{
    if (segment.rfind("https://", 0) == 0 || segment.rfind("http://", 0) == 0) return segment;
    const size_t scheme_end = playlist_url.find("//");
    const size_t host_end = scheme_end == std::string::npos ? std::string::npos : playlist_url.find('/', scheme_end + 2);
    if (!segment.empty() && segment[0] == '/') {
        return host_end == std::string::npos ? playlist_url + segment : playlist_url.substr(0, host_end) + segment;
    }
    const size_t slash = playlist_url.find_last_of('/');
    return (slash == std::string::npos ? std::string{} : playlist_url.substr(0, slash + 1)) + segment;
}

size_t parse_hls_segments(const std::string &playlist, const std::string &playlist_url,
                          std::array<std::string, kMaxHlsSegments> &segments)
{
    size_t count = 0;
    size_t start = 0;
    while (start < playlist.size() && count < segments.size()) {
        const size_t end = playlist.find('\n', start);
        const size_t line_end = end == std::string::npos ? playlist.size() : end;
        size_t length = line_end - start;
        if (length && playlist[start + length - 1] == '\r') --length;
        if (length && playlist[start] != '#') {
            segments[count++] = make_hls_segment_url(playlist_url, playlist.substr(start, length));
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return count;
}

bool download_hls_segment(const std::string &url, uint8_t *buffer, size_t capacity, size_t &size)
{
    size = 0;
    int status_code = 0;
    esp_err_t error = ESP_OK;
    esp_http_client_handle_t client = open_http_get(url, status_code, error);
    if (!client) {
        ESP_LOGE(kTag, "HLS segment request failed: %s (HTTP %d)", esp_err_to_name(error), status_code);
        return false;
    }
    while (size < capacity) {
        const int count = esp_http_client_read(client, reinterpret_cast<char *>(buffer + size),
                                               std::min<size_t>(8192, capacity - size));
        if (count == 0) break;
        if (count < 0) {
            ESP_LOGE(kTag, "HLS segment read failed: %d", count);
            size = 0;
            break;
        }
        size += static_cast<size_t>(count);
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    if (size == capacity) {
        ESP_LOGE(kTag, "HLS segment exceeded %u bytes", static_cast<unsigned>(capacity));
        size = 0;
    }
    return size != 0;
}

bool extract_aac_from_mpeg_ts(const uint8_t *transport_stream, size_t transport_size,
                              uint8_t *aac_data, size_t aac_capacity, size_t &aac_size)
{
    constexpr size_t kTsPacketSize = 188;
    int audio_pid = -1;
    size_t pes_remaining = 0;
    bool unbounded_pes = false;
    aac_size = 0;
    for (size_t offset = 0; offset + kTsPacketSize <= transport_size; offset += kTsPacketSize) {
        const uint8_t *packet = transport_stream + offset;
        if (packet[0] != 0x47) return false;
        const int pid = ((packet[1] & 0x1f) << 8) | packet[2];
        const bool payload_start = (packet[1] & 0x40) != 0;
        const int adaptation_control = (packet[3] >> 4) & 0x03;
        if (!(adaptation_control & 0x01)) continue;
        size_t payload_offset = 4;
        if (adaptation_control & 0x02) payload_offset += 1 + packet[4];
        if (payload_offset >= kTsPacketSize) continue;
        const uint8_t *payload = packet + payload_offset;
        size_t payload_size = kTsPacketSize - payload_offset;

        if (payload_start && payload_size >= 9 && payload[0] == 0 && payload[1] == 0 && payload[2] == 1 &&
            payload[3] >= 0xc0 && payload[3] <= 0xdf && audio_pid < 0) {
            audio_pid = pid;
        }
        if (pid != audio_pid || audio_pid < 0) continue;

        if (payload_start) {
            if (payload_size < 9 || payload[0] != 0 || payload[1] != 0 || payload[2] != 1) continue;
            const size_t header_size = 9 + payload[8];
            if (header_size > payload_size) continue;
            const size_t packet_length = (static_cast<size_t>(payload[4]) << 8) | payload[5];
            const size_t header_payload = 3 + payload[8];
            unbounded_pes = packet_length == 0;
            if (!unbounded_pes && packet_length < header_payload) continue;
            pes_remaining = unbounded_pes ? SIZE_MAX : packet_length - header_payload;
            payload += header_size;
            payload_size -= header_size;
        }
        if (!unbounded_pes) payload_size = std::min(payload_size, pes_remaining);
        if (payload_size > aac_capacity - aac_size) return false;
        std::memcpy(aac_data + aac_size, payload, payload_size);
        aac_size += payload_size;
        if (!unbounded_pes) pes_remaining -= payload_size;
    }
    return aac_size > 0;
}

bool test_internet_connection()
{
    esp_http_client_config_t config{};
    config.url = "https://example.com/";
    config.timeout_ms = 8000;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.user_agent = "p4Radio/1.0 ESP-IDF";
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(kTag, "Internet probe: HTTP client initialization failed");
        set_status("Internet test could not start");
        return false;
    }

    const esp_err_t err = esp_http_client_perform(client);
    const int status_code = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    if (err == ESP_OK && status_code >= 200 && status_code < 400) {
        ESP_LOGI(kTag, "Internet probe passed: example.com returned HTTP %d", status_code);
        set_status("Internet OK; loading BBC playlist");
        return true;
    }

    ESP_LOGE(kTag, "Internet probe failed: %s (HTTP %d)", esp_err_to_name(err), status_code);
    set_status("Internet test failed; check DNS or router access");
    return false;
}

bool open_speaker(const esp_audio_simple_dec_info_t &info)
{
    if (!g_speaker) {
        g_speaker = bsp_audio_codec_speaker_init();
        if (!g_speaker) return false;
    }
    esp_codec_dev_sample_info_t sample{};
    sample.sample_rate = info.sample_rate;
    sample.channel = info.channel;
    sample.bits_per_sample = info.bits_per_sample;
    sample.channel_mask = 0;
    esp_codec_dev_close(g_speaker);
    if (esp_codec_dev_open(g_speaker, &sample) != ESP_CODEC_DEV_OK) {
        ESP_LOGE(kTag, "ES8311 open failed for %lu Hz", static_cast<unsigned long>(info.sample_rate));
        return false;
    }
    esp_codec_dev_set_out_vol(g_speaker, g_volume.load());
    return true;
}

bool play_stream(const Station &station, int station_index)
{
    set_status((std::string("Loading ") + station.name).c_str());
    const std::string playlist_url = resolve_station_url(station);
    esp_audio_dec_register_default();
    esp_audio_simple_dec_register_default();
    esp_audio_simple_dec_cfg_t decoder_config{};
    decoder_config.dec_type = ESP_AUDIO_SIMPLE_DEC_TYPE_AAC;
    decoder_config.use_frame_dec = false;
    esp_audio_simple_dec_handle_t decoder = nullptr;
    if (esp_audio_simple_dec_open(&decoder_config, &decoder) != ESP_AUDIO_ERR_OK) {
        esp_audio_simple_dec_unregister_default();
        esp_audio_dec_unregister_default();
        set_status("AAC decoder could not start");
        return false;
    }
    constexpr size_t output_size = 8192;
    auto *transport_stream = static_cast<uint8_t *>(heap_caps_malloc(kHlsSegmentMaxSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    auto *aac_data = static_cast<uint8_t *>(heap_caps_malloc(kHlsSegmentMaxSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    auto *output = static_cast<uint8_t *>(heap_caps_malloc(output_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!transport_stream || !aac_data || !output) {
        heap_caps_free(transport_stream);
        heap_caps_free(aac_data);
        heap_caps_free(output);
        esp_audio_simple_dec_close(decoder);
        esp_audio_simple_dec_unregister_default();
        esp_audio_dec_unregister_default();
        set_status("Not enough PSRAM for audio buffers");
        return false;
    }

    bool audio_open = false;
    bool first_audio = true;
    std::string last_segment;
    while (g_connected.load() && g_playback_requested.load() && g_station.load() == station_index) {
        const std::string playlist = fetch_hls_playlist(playlist_url);
        std::array<std::string, kMaxHlsSegments> segments{};
        const size_t segment_count = parse_hls_segments(playlist, playlist_url, segments);
        if (!segment_count) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        size_t segment_index = segment_count - 1;
        bool wait_for_next = false;
        if (!last_segment.empty()) {
            for (size_t i = 0; i < segment_count; ++i) {
                if (segments[i] == last_segment) {
                    if (i + 1 < segment_count) segment_index = i + 1;
                    else wait_for_next = true;
                    break;
                }
            }
        }
        if (wait_for_next) {
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        size_t transport_size = 0;
        if (!download_hls_segment(segments[segment_index], transport_stream, kHlsSegmentMaxSize, transport_size)) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        last_segment = segments[segment_index];
        size_t aac_size = 0;
        if (!extract_aac_from_mpeg_ts(transport_stream, transport_size, aac_data, kHlsSegmentMaxSize, aac_size)) {
            ESP_LOGE(kTag, "Could not extract AAC from HLS segment (%u bytes)", static_cast<unsigned>(transport_size));
            set_status("BBC segment format not recognized");
            break;
        }

        esp_audio_simple_dec_raw_t raw{};
        raw.buffer = aac_data;
        raw.len = aac_size;
        while (raw.len) {
            esp_audio_simple_dec_out_t decoded{};
            decoded.buffer = output;
            decoded.len = output_size;
            const esp_audio_err_t decode_result = esp_audio_simple_dec_process(decoder, &raw, &decoded);
            if (decode_result == ESP_AUDIO_ERR_BUFF_NOT_ENOUGH) {
                set_status("AAC frame exceeds decoder buffer");
                raw.len = 0;
                break;
            }
            if (decode_result != ESP_AUDIO_ERR_OK) {
                ESP_LOGW(kTag, "AAC decode returned %d", decode_result);
                raw.len = 0;
                break;
            }
            if (decoded.decoded_size) {
                if (first_audio) {
                    esp_audio_simple_dec_info_t info{};
                    if (esp_audio_simple_dec_get_info(decoder, &info) != ESP_AUDIO_ERR_OK || !open_speaker(info)) {
                        set_status("Audio codec setup failed");
                        raw.len = 0;
                        break;
                    }
                    audio_open = true;
                    first_audio = false;
                    ESP_LOGI(kTag, "Playing %s as AAC HLS", station.name);
                    set_status((std::string("Playing ") + station.name).c_str());
                }
                if (audio_open) esp_codec_dev_write(g_speaker, decoded.buffer, decoded.decoded_size);
            }
            if (raw.consumed == 0) break;
            raw.len -= raw.consumed;
            raw.buffer += raw.consumed;
            raw.consumed = 0;
        }
        if (!first_audio && !audio_open) {
            set_status("AAC audio output could not start");
            break;
        }
    }
    if (audio_open && g_speaker) {
        esp_codec_dev_close(g_speaker);
    }
    esp_audio_simple_dec_close(decoder);
    esp_audio_simple_dec_unregister_default();
    esp_audio_dec_unregister_default();
    heap_caps_free(transport_stream);
    heap_caps_free(aac_data);
    heap_caps_free(output);
    return true;
}

void playback_task(void *)
{
    int previous_station = -1;
    while (true) {
        if (g_sleep_requested.load()) {
            if (g_speaker) {
                esp_codec_dev_close(g_speaker);
                bsp_audio_deinit();
                g_speaker = nullptr;
            }
            if (g_connected.load()) esp_wifi_stop();
            bsp_display_backlight_off();
            set_status("Sleeping; press RESET to wake");
            vTaskDelay(pdMS_TO_TICKS(250));
            esp_deep_sleep_start();
        }
        if (!g_connected.load() || !g_playback_requested.load()) {
            vTaskDelay(pdMS_TO_TICKS(250));
            continue;
        }
        if (g_internet_probe_pending.exchange(false)) {
            test_internet_connection();
        }
        const int selected = g_station.load();
        if (selected != previous_station) {
            previous_station = selected;
            play_stream(kStations[selected], selected);
        } else {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
}

void radio_button(lv_event_t *event)
{
    const intptr_t action = reinterpret_cast<intptr_t>(lv_event_get_user_data(event));
    switch (action) {
    case 0: // previous station
        g_station.store((g_station.load() + kStationCount - 1) % kStationCount);
        g_playback_requested.store(true);
        save_preferences();
        break;
    case 1: // next station
        g_station.store((g_station.load() + 1) % kStationCount);
        g_playback_requested.store(true);
        save_preferences();
        break;
    case 2:
        g_volume.store(std::max(kMinVolume, g_volume.load() - 5));
        save_preferences();
        if (g_speaker) esp_codec_dev_set_out_vol(g_speaker, g_volume.load());
        break;
    case 3:
        g_volume.store(std::min(kMaxVolume, g_volume.load() + 5));
        save_preferences();
        if (g_speaker) esp_codec_dev_set_out_vol(g_speaker, g_volume.load());
        break;
    case 4:
        g_playback_requested.store(false);
        g_sleep_requested.store(true);
        break;
    }
}

lv_obj_t *add_button(lv_obj_t *parent, const char *caption, int x, int y, int width, int height, intptr_t action)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_set_size(button, width, height);
    lv_obj_set_pos(button, x, y);
    lv_obj_set_style_bg_color(button, lv_color_hex(action == 4 ? 0x9a4b53 : 0x287b71), 0);
    lv_obj_set_style_radius(button, 18, 0);
    lv_obj_add_event_cb(button, radio_button, LV_EVENT_CLICKED, reinterpret_cast<void *>(action));
    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, caption);
    lv_obj_center(label);
    return button;
}

void refresh_ui(lv_timer_t *)
{
    if (g_station_label) lv_label_set_text(g_station_label, kStations[g_station.load()].name);
    if (g_status_label) {
        const std::string text = status_copy();
        lv_label_set_text(g_status_label, text.c_str());
    }
    if (g_volume_label) {
        char volume[24];
        std::snprintf(volume, sizeof(volume), "VOLUME  %d%%", g_volume.load());
        lv_label_set_text(g_volume_label, volume);
    }
}

void build_ui()
{
    lv_display_t *display = bsp_display_start();
    if (!display) {
        ESP_LOGE(kTag, "Board display initialization failed");
        return;
    }
    bsp_display_brightness_set(100);
    if (!bsp_display_lock(-1)) return;
    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x101827), 0);
    lv_obj_set_style_text_color(screen, lv_color_hex(0xf4f7fb), 0);

    lv_obj_t *brand = lv_label_create(screen);
    lv_label_set_text(brand, "P4 RADIO");
    lv_obj_set_style_text_color(brand, lv_color_hex(0x60d6ad), 0);
    lv_obj_set_style_text_font(brand, &lv_font_montserrat_24, 0);
    lv_obj_align(brand, LV_ALIGN_TOP_MID, 0, 40);

    g_station_label = lv_label_create(screen);
    lv_label_set_text(g_station_label, kStations[g_station.load()].name);
    lv_obj_set_style_text_font(g_station_label, &lv_font_montserrat_40, 0);
    lv_obj_set_width(g_station_label, 650);
    lv_obj_set_style_text_align(g_station_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(g_station_label, LV_LABEL_LONG_DOT);
    lv_obj_align(g_station_label, LV_ALIGN_TOP_MID, 0, 145);

    g_status_label = lv_label_create(screen);
    lv_label_set_text(g_status_label, "Starting radio");
    lv_obj_set_style_text_color(g_status_label, lv_color_hex(0xa9b8ca), 0);
    lv_obj_set_width(g_status_label, 650);
    lv_obj_set_style_text_align(g_status_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(g_status_label, LV_ALIGN_TOP_MID, 0, 230);

    add_button(screen, "PREV", 100, 330, 200, 110, 0);
    add_button(screen, "NEXT", 420, 330, 200, 110, 1);
    add_button(screen, "VOL -", 140, 485, 140, 95, 2);
    add_button(screen, "VOL +", 440, 485, 140, 95, 3);

    g_volume_label = lv_label_create(screen);
    char volume[24];
    std::snprintf(volume, sizeof(volume), "VOLUME  %d%%", g_volume.load());
    lv_label_set_text(g_volume_label, volume);
    lv_obj_set_style_text_color(g_volume_label, lv_color_hex(0xa9b8ca), 0);
    lv_obj_align(g_volume_label, LV_ALIGN_TOP_MID, 0, 595);
    add_button(screen, "POWER", 250, 635, 220, 64, 4);

    lv_timer_create(refresh_ui, 500, nullptr);
    bsp_display_unlock();
}

void initialize_audio()
{
    g_speaker = bsp_audio_codec_speaker_init();
    if (!g_speaker) {
        set_status("Audio codec initialization failed");
    }
}

} // namespace

extern "C" void app_main(void)
{
    esp_err_t nvs_result = nvs_flash_init();
    if (nvs_result == ESP_ERR_NVS_NO_FREE_PAGES || nvs_result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_result = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs_result);
    load_preferences();
    build_ui();
    initialize_audio();
    const bool network_ready = ensure_network_stack();
    if (!network_ready) {
        // Ethernet remains a possible fallback on the 86-panel assembly, but this build
        // intentionally reports the missing C6 link rather than pretending that P4 has Wi-Fi.
        set_status("No C6 Wi-Fi: check the coprocessor firmware or use Ethernet");
    }
    xTaskCreate(playback_task, "radio_playback", 8192, nullptr, 5, nullptr);
}
