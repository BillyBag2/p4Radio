#include "radio.hpp"
#include "button_audio.hpp"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "esp_timer.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_wifi_remote.h"
#include "esp_hosted.h"
#include "freertos/task.h"
#include "nvs.h"
#include "mdns.h"

namespace p4radio {

std::atomic<int> g_station_retries{0};
std::atomic<int64_t> g_join_deadline_us{0};
std::atomic<bool> g_join_allowed{true};
httpd_handle_t g_http_server = nullptr;
esp_netif_t *g_sta_netif = nullptr;
esp_netif_t *g_ap_netif = nullptr;

bool wifi_join_in_progress()
{
    return g_saved_ssid[0] && g_join_allowed.load() && g_join_deadline_us.load() != 0 &&
        !g_connected.load() && !g_sleep_requested.load();
}

void apply_hostname()
{
    const std::string name = hostname_copy();
    if (g_sta_netif) esp_netif_set_hostname(g_sta_netif, name.c_str());
    if (g_ap_netif) esp_netif_set_hostname(g_ap_netif, name.c_str());
    const esp_err_t result = mdns_hostname_set(name.c_str());
    if (result != ESP_OK) ESP_LOGW(kTag, "mDNS hostname: %s", esp_err_to_name(result));
}

std::string url_decode(const char *input)
{
    std::string output;
    for (size_t i = 0; input[i] != '\0'; ++i) {
        if (input[i] == '+' ) {
            output.push_back(' ');
        } else if (input[i] == '%' && input[i + 1] && input[i + 2] && std::isxdigit(static_cast<unsigned char>(input[i + 1])) &&
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

esp_err_t portal_page(httpd_req_t *request)
{
    ESP_LOGI(kTag, "Setup page requested: %s", request->uri);
    static constexpr char page[] = R"HTML(<!doctype html><html><head><meta name="viewport" content="width=device-width,initial-scale=1"><title>p4Radio Wi-Fi setup</title><style>
body{font:17px system-ui;background:#101827;color:#f3f6fb;margin:0;padding:24px}main{max-width:480px;margin:auto;background:#1b2638;padding:24px;border-radius:18px}h1{margin-top:0}label{display:block;margin:16px 0 6px}input,select,button{box-sizing:border-box;width:100%;padding:13px;border:0;border-radius:9px;font:inherit}button{background:#60d6ad;font-weight:700;margin-top:18px}small{color:#a8b7ca}</style></head><body><main><h1>Connect p4Radio</h1><p><a href="/" style="color:#60d6ad">Radio settings</a></p><p>Choose your Wi-Fi network and enter its password. Your details stay on this radio.</p><form method="post" action="/connect"><label for="hostname">Host name</label><input id="hostname" name="hostname" maxlength="32" pattern="[a-z0-9]+([a-z0-9\-]*[a-z0-9])?" value="HOSTNAME_VALUE" required><small>Open http://&lt;hostname&gt;.local/ on your network.</small><label for="ssid">Wi-Fi network</label><select id="networks" onchange="document.getElementById('ssid').value=this.value"><option value="">Scanning…</option></select><input id="ssid" name="ssid" maxlength="32" placeholder="Network name" required><label for="password">Password</label><input id="password" name="password" type="password" maxlength="64" autocomplete="current-password"><button>Save and connect</button></form><p><small>If the list is empty, type the network name. This open setup network does not provide internet.</small></p></main><script>fetch('/api/scan').then(r=>r.json()).then(a=>{let s=document.getElementById('networks');s.innerHTML='<option value="">Select a network…</option>';a.forEach(n=>{let o=document.createElement('option');o.value=n;o.textContent=n;s.appendChild(o)})}).catch(()=>{});</script></body></html>)HTML";
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    std::string rendered = page;
    const size_t marker = rendered.find("HOSTNAME_VALUE");
    if (marker != std::string::npos) rendered.replace(marker, std::strlen("HOSTNAME_VALUE"), hostname_copy());
    return httpd_resp_send(request, rendered.c_str(), rendered.size());
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
    // Stop the previous join before replacing credentials, without retrying its
    // disconnect event. The deadline also covers a connect RPC that reports failure.
    g_join_allowed.store(false);
    g_join_deadline_us.store(0);
    esp_wifi_disconnect();
    wifi_config_t config{};
    std::snprintf(reinterpret_cast<char *>(config.sta.ssid), sizeof(config.sta.ssid), "%s", ssid);
    std::snprintf(reinterpret_cast<char *>(config.sta.password), sizeof(config.sta.password), "%s", password);
    config.sta.threshold.authmode = WIFI_AUTH_OPEN;
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &config));
    g_connected.store(false);
    g_station_retries.store(0);
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    g_join_deadline_us.store(esp_timer_get_time() + kJoinTimeoutUs);
    g_join_allowed.store(true);
    const esp_err_t error = esp_wifi_connect();
    if (error != ESP_OK) ESP_LOGW(kTag, "Wi-Fi join request: %s", esp_err_to_name(error));
    set_status("Connecting to Wi-Fi…");
}

esp_err_t connect_network(httpd_req_t *request)
{
    if (request->content_len <= 0 || request->content_len > 512) {
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
    std::string hostname = form_value(body, "hostname");
    std::transform(hostname.begin(), hostname.end(), hostname.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    if (!valid_hostname(hostname)) return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Host name must be 1-32 letters, digits or internal hyphens");
    nvs_handle_t handle;
    esp_err_t saved = nvs_open("p4radio", NVS_READWRITE, &handle);
    if (saved == ESP_OK) {
        saved = nvs_set_str(handle, "hostname", hostname.c_str());
        if (saved == ESP_OK) saved = nvs_commit(handle);
        nvs_close(handle);
    }
    if (saved != ESP_OK) return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Could not save hostname");
    portENTER_CRITICAL(&g_status_mux);
    std::snprintf(g_hostname, sizeof(g_hostname), "%s", hostname.c_str());
    portEXIT_CRITICAL(&g_status_mux);
    apply_hostname();
    save_wifi_credentials(ssid.c_str(), password.c_str());
    const bool dismissing_qr = g_portal_active.load() && !g_setup_submitted.load();
    g_setup_submitted.store(true);
    if (dismissing_qr) request_connect_sound();
    // Post/Redirect/Get: refresh must never repeat credential writes or joins.
    httpd_resp_set_status(request, "303 See Other");
    httpd_resp_set_hdr(request, "Location", "/connecting");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    const esp_err_t result = httpd_resp_send(request, nullptr, 0);
    configure_station(ssid.c_str(), password.c_str());
    return result;
}

esp_err_t connecting_page(httpd_req_t *request)
{
    static constexpr char page[] = R"HTML(<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Connecting p4Radio</title><style>
body{font:17px system-ui;background:#101827;color:#f3f6fb;padding:24px}main{max-width:480px;margin:40px auto;background:#1b2638;padding:24px;border-radius:18px}a{color:#60d6ad}.spinner{width:36px;height:36px;border:4px solid #435269;border-top-color:#60d6ad;border-radius:50%;animation:spin 1s linear infinite}@keyframes spin{to{transform:rotate(360deg)}}@media(prefers-reduced-motion:reduce){.spinner{animation:none}}</style></head><body><main aria-busy="true"><div class="spinner" aria-hidden="true"></div><h1>Connecting...</h1><p>Your Wi-Fi details are saved. Please wait while the radio connects.</p><p id="progress" role="status">Waiting for the radio to reconnect...</p><p>The setup network closes after a successful connection. Rejoin your usual Wi-Fi network if necessary, then open the radio's bookmarked hostname URL.</p><p><a href="/wifi">Return to Wi-Fi settings</a></p></main><script>
const progress=document.getElementById('progress');
async function checkConnection(){
  try{
    const response=await fetch('/api/connection',{cache:'no-store',signal:AbortSignal.timeout(800)});
    if(!response.ok)throw new Error('Unavailable');
    const state=await response.json();
    if(state.connected){progress.textContent='Connected. Opening the home page...';window.location.replace('/');return;}
    if(!state.connecting){document.querySelector('main').setAttribute('aria-busy','false');document.querySelector('.spinner').style.display='none';progress.textContent='Connection failed. Return to setup to check your Wi-Fi details.';return;}
    progress.textContent='Connecting to Wi-Fi...';
  }catch(error){progress.textContent='Waiting for the radio. If necessary, reconnect your phone to your home Wi-Fi.';}
  setTimeout(checkConnection,1000);
}
setTimeout(checkConnection,3000);
</script></body></html>)HTML";
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_send(request, page, HTTPD_RESP_USE_STRLEN);
}

esp_err_t connection_state(httpd_req_t *request)
{
    const bool connected = g_connected.load();
    const bool connecting = g_join_allowed.load() && g_join_deadline_us.load() != 0;
    const char *body = connected ? "{\"connected\":true,\"connecting\":false}" :
        connecting ? "{\"connected\":false,\"connecting\":true}" : "{\"connected\":false,\"connecting\":false}";
    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_send(request, body, HTTPD_RESP_USE_STRLEN);
}

esp_err_t settings_page(httpd_req_t *request)
{
    static constexpr char page[] = R"HTML(<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Radio settings</title><style>
body{font:17px system-ui;background:#101827;color:#f3f6fb;margin:0;padding:24px}main{max-width:480px;margin:auto;background:#1b2638;padding:24px;border-radius:18px}label{display:block;margin:18px 0 8px}input[type=text],select,button{box-sizing:border-box;width:100%;padding:13px;border:0;border-radius:9px;font:inherit}input[type=checkbox]{width:22px;height:22px;vertical-align:middle;margin-right:10px}button{background:#60d6ad;margin-top:24px;font-weight:bold}a{color:#60d6ad}small{display:block;color:#c4cede;margin-top:8px}</style></head><body><main><h1>Radio settings</h1><p><a href="/wifi">Wi-Fi settings</a></p>SAVED_MESSAGE<form method="post" action="/settings"><label for="hostname">Host name</label><input id="hostname" type="text" name="hostname" value="HOSTNAME_VALUE" minlength="1" maxlength="32" pattern="[a-z0-9]+([a-z0-9\-]*[a-z0-9])?" autocapitalize="none" spellcheck="false" oninput="this.value=this.value.toLowerCase()" required><small>Use 1-32 letters (a-z), digits or hyphens. Start and end with a letter or digit. Do not include .local.</small><label><input type="checkbox" name="volume_bar" value="1" VOLUME_CHECKED>Show volume bar</label><label><input type="checkbox" name="buffer_bar" value="1" BUFFER_CHECKED>Show buffer bar</label><label for="max_bitrate">Maximum stream bitrate</label><select id="max_bitrate" name="max_bitrate">BITRATE_OPTIONS</select><small>Limits advertised stream bitrate. Changing this retunes the radio. Lower limits may leave some stations unavailable.</small><button>Save settings</button></form><p>Radio address: <a href="http://HOSTNAME_VALUE.local/">http://HOSTNAME_VALUE.local/</a></p></main></body></html>)HTML";
    std::string rendered = page;
    const auto replace_all = [&rendered](const std::string &token, const std::string &value) {
        size_t position = 0;
        while ((position = rendered.find(token, position)) != std::string::npos) {
            rendered.replace(position, token.size(), value);
            position += value.size();
        }
    };
    std::string options;
    for (int rate : kStandardBitrates) {
        options += "<option value=\"" + std::to_string(rate) + "\"" +
            (rate == g_max_bitrate_kbps.load() ? " selected" : "") + ">" +
            (rate == 0 ? std::string("Unlimited") : std::to_string(rate) + " kbit/s") + "</option>";
    }
    replace_all("BITRATE_OPTIONS", options);
    replace_all("HOSTNAME_VALUE", hostname_copy());
    replace_all("VOLUME_CHECKED", g_show_volume_bar.load() ? "checked" : "");
    replace_all("BUFFER_CHECKED", g_show_buffer_bar.load() ? "checked" : "");
    char query[64] = {}, saved[8] = {};
    const bool saved_ok = httpd_req_get_url_query_str(request, query, sizeof(query)) == ESP_OK &&
        httpd_query_key_value(query, "saved", saved, sizeof(saved)) == ESP_OK && std::strcmp(saved, "1") == 0;
    replace_all("SAVED_MESSAGE", saved_ok ? "<p role=\"status\">Settings saved. Update your bookmark if you changed the host name.</p>" : "");
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_send(request, rendered.c_str(), rendered.size());
}

esp_err_t update_settings(httpd_req_t *request)
{
    if (request->content_len <= 0 || request->content_len > 256)
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Invalid settings form");
    std::string body(static_cast<size_t>(request->content_len), '\0');
    size_t received = 0;
    while (received < body.size()) {
        const int count = httpd_req_recv(request, body.data() + received, body.size() - received);
        if (count <= 0) return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Incomplete settings form");
        received += count;
    }
    std::string hostname = form_value(body, "hostname");
    std::transform(hostname.begin(), hostname.end(), hostname.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    const std::string volume = form_value(body, "volume_bar"), buffer = form_value(body, "buffer_bar");
    if (!valid_hostname(hostname) || (!volume.empty() && volume != "1") || (!buffer.empty() && buffer != "1"))
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Use a 1-32 character host name with letters, digits and internal hyphens");
    const std::string rate_text = form_value(body, "max_bitrate");
    int max_bitrate = -1;
    for (int rate : kStandardBitrates) if (rate_text == std::to_string(rate)) max_bitrate = rate;
    if (max_bitrate < 0) return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Select a standard maximum bitrate");
    const esp_err_t result = save_web_settings(hostname, volume == "1", buffer == "1", max_bitrate);
    if (result != ESP_OK) {
        ESP_LOGE(kTag, "Save web settings: %s", esp_err_to_name(result));
        return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Could not save settings");
    }
    apply_hostname();
    httpd_resp_set_status(request, "303 See Other");
    httpd_resp_set_hdr(request, "Location", "/?saved=1");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_send(request, nullptr, 0);
}

void start_setup_http()
{
    if (g_http_server) return;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = kHttpPort;
    config.stack_size = 6144;
    // Phone probes and browser connections must not exhaust the small socket pool.
    config.lru_purge_enable = true;
    config.recv_wait_timeout = 5;
    config.send_wait_timeout = 5;
    ESP_ERROR_CHECK(httpd_start(&g_http_server, &config));
    const httpd_uri_t root{.uri = "/", .method = HTTP_GET, .handler = settings_page, .user_ctx = nullptr};
    const httpd_uri_t scan{.uri = "/api/scan", .method = HTTP_GET, .handler = scan_networks, .user_ctx = nullptr};
    const httpd_uri_t connect{.uri = "/connect", .method = HTTP_POST, .handler = connect_network, .user_ctx = nullptr};
    ESP_ERROR_CHECK(httpd_register_uri_handler(g_http_server, &root));
    const httpd_uri_t wifi{.uri = "/wifi", .method = HTTP_GET, .handler = portal_page, .user_ctx = nullptr};
    ESP_ERROR_CHECK(httpd_register_uri_handler(g_http_server, &wifi));
    ESP_ERROR_CHECK(httpd_register_uri_handler(g_http_server, &scan));
    ESP_ERROR_CHECK(httpd_register_uri_handler(g_http_server, &connect));
    const httpd_uri_t connecting{.uri = "/connecting", .method = HTTP_GET, .handler = connecting_page, .user_ctx = nullptr};
    ESP_ERROR_CHECK(httpd_register_uri_handler(g_http_server, &connecting));
    const httpd_uri_t state{.uri = "/api/connection", .method = HTTP_GET, .handler = connection_state, .user_ctx = nullptr};
    ESP_ERROR_CHECK(httpd_register_uri_handler(g_http_server, &state));
    const httpd_uri_t settings_get{.uri = "/settings", .method = HTTP_GET, .handler = settings_page, .user_ctx = nullptr};
    const httpd_uri_t settings_post{.uri = "/settings", .method = HTTP_POST, .handler = update_settings, .user_ctx = nullptr};
    ESP_ERROR_CHECK(httpd_register_uri_handler(g_http_server, &settings_get));
    ESP_ERROR_CHECK(httpd_register_uri_handler(g_http_server, &settings_post));
}

void start_portal()
{
    if (g_portal_active.exchange(true)) {
        return;
    }
    g_setup_submitted.store(false);
    wifi_config_t ap{};
    std::snprintf(reinterpret_cast<char *>(ap.ap.ssid), sizeof(ap.ap.ssid), "%s", kApSsid);
    ap.ap.ssid_len = std::strlen(kApSsid);
    ap.ap.channel = 6;
    ap.ap.max_connection = 4;
    ap.ap.authmode = WIFI_AUTH_OPEN;
    // Saved-network startup uses STA-only mode. Enable the AP interface before
    // configuring it, otherwise the fallback path returns ESP_ERR_WIFI_MODE.
    esp_err_t error = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (error == ESP_OK) error = esp_wifi_set_config(WIFI_IF_AP, &ap);
    if (error != ESP_OK) {
        g_portal_active.store(false);
        ESP_LOGE(kTag, "Setup AP failed: %s", esp_err_to_name(error));
        set_status("Setup AP could not start; restart to retry");
        return;
    }

    start_setup_http();
    set_status("Setup Wi-Fi: join p4Radio-Setup");
}

void stop_portal()
{
    g_portal_active.store(false);
    g_ap_clients.store(0);
    esp_wifi_set_mode(WIFI_MODE_STA);
}

void wifi_event_handler(void *, esp_event_base_t base, int32_t id, void *)
{
    if (base == WIFI_EVENT && (id == WIFI_EVENT_AP_STACONNECTED || id == WIFI_EVENT_AP_STADISCONNECTED)) {
        wifi_sta_list_t clients{};
        if (esp_wifi_ap_get_sta_list(&clients) == ESP_OK) g_ap_clients.store(clients.num);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        if (g_saved_ssid[0] && g_join_allowed.load()) {
            g_join_deadline_us.store(esp_timer_get_time() + kJoinTimeoutUs);
            esp_wifi_connect();
        }
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        g_connected.store(false);
        if (!g_join_allowed.load()) return;
        if (g_join_deadline_us.load() == 0) g_join_deadline_us.store(esp_timer_get_time() + kJoinTimeoutUs);
        if (g_saved_ssid[0] && g_station_retries.fetch_add(1) < 5) {
            vTaskDelay(pdMS_TO_TICKS(400));
            esp_wifi_connect();
        } else if (!g_portal_active.load()) {
            g_join_allowed.store(false);
            g_join_deadline_us.store(0);
            start_portal();
        } else {
            g_join_allowed.store(false);
            g_join_deadline_us.store(0);
            g_setup_submitted.store(false);
            set_status("Wi-Fi join failed; retry in setup page");
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        if (!g_join_allowed.load()) { esp_wifi_disconnect(); return; }
        g_join_deadline_us.store(0);
        g_setup_submitted.store(false);
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

void wifi_join_watchdog(void *)
{
    while (true) {
        int64_t deadline = g_join_deadline_us.load();
        if (deadline != 0 && esp_timer_get_time() >= deadline &&
            g_join_deadline_us.compare_exchange_strong(deadline, 0)) {
            g_join_allowed.store(false);
            g_connected.store(false);
            esp_wifi_disconnect();
            if (!g_portal_active.load()) start_portal();
            g_setup_submitted.store(false);
            set_status("Wi-Fi timed out; check details in setup page");
        }
        vTaskDelay(pdMS_TO_TICKS(250));
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
    g_sta_netif = esp_netif_create_default_wifi_sta();
    g_ap_netif = esp_netif_create_default_wifi_ap();
    ESP_ERROR_CHECK(mdns_init());
    apply_hostname();
    ESP_ERROR_CHECK(mdns_service_add(nullptr, "_http", "_tcp", kHttpPort, nullptr, 0));
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
    start_setup_http();
    if (xTaskCreate(wifi_join_watchdog, "wifi_join", 4096, nullptr, 3, nullptr) != pdPASS) {
        set_status("Wi-Fi timeout task could not start");
        return false;
    }
    ESP_ERROR_CHECK(esp_wifi_start());

    if (g_saved_ssid[0]) {
        set_status("Connecting to saved Wi-Fi...");
    } else {
        start_portal();
    }
    return true;
}


} // namespace p4radio
