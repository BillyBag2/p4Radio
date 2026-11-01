#include "stream_input.hpp"
#include "station_rules.hpp"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <vector>
#include <strings.h>
#include "esp_crt_bundle.h"
#include "esp_timer.h"

namespace p4radio {
StreamInput::~StreamInput() { close(); }
void StreamInput::close()
{
    if (client) { esp_http_client_close(client); esp_http_client_cleanup(client); client = nullptr; }
}
esp_err_t StreamInput::event(esp_http_client_event_t *event)
{
    auto &self = *static_cast<StreamInput *>(event->user_data);
    if (event->event_id == HTTP_EVENT_ON_HEADER) {
        if (!strcasecmp(event->header_key, "Content-Type")) {
            self.content_type = event->header_value;
            for (char &c : self.content_type) c = std::tolower(static_cast<unsigned char>(c));
        }
        if (!strcasecmp(event->header_key, "icy-metaint")) {
            const long value = std::strtol(event->header_value, nullptr, 10);
            if (value > 0 && value <= 16 * 1024 * 1024) self.interval = value;
        }
    }
    return ESP_OK;
}
bool StreamInput::open(std::string address, const Station &station, int slot)
{
    selected_slot = slot; selected_id = station.id;
    playlist.clear(); hls = mp3 = false;
    for (int depth = 0; depth < 6; ++depth) {
        if (!g_connected.load() || !g_playback_requested.load() || g_sleep_requested.load() || g_scan_requested.load() || g_station.load() != slot || g_station_token.load() != station.id) return false;
        close(); pending.clear(); content_type.clear(); interval = 0; icy.reset(0);
        url = address;
        if (!station_rules::valid_url(url)) return false;
        esp_http_client_config_t config{};
        config.url = url.c_str(); config.timeout_ms = 3000; config.buffer_size = 8192;
        config.crt_bundle_attach = esp_crt_bundle_attach;
        config.user_agent = "p4Radio/1.0 ESP-IDF";
        config.disable_auto_redirect = true; config.event_handler = event; config.user_data = this;
        client = esp_http_client_init(&config);
        if (!client) return false;
        esp_http_client_set_header(client, "Icy-MetaData", "0");
        if (esp_http_client_open(client, 0) != ESP_OK || esp_http_client_fetch_headers(client) < 0) return false;
        const int status = esp_http_client_get_status_code(client);
        if (status >= 300 && status < 400) {
            if (esp_http_client_set_redirection(client) != ESP_OK) return false;
            std::vector<char> redirected(2048);
            if (esp_http_client_get_url(client, redirected.data(), redirected.size()) != ESP_OK) return false;
            address = redirected.data(); continue;
        }
        if (status < 200 || status >= 300) return false;
        icy.reset(interval);
        uint8_t prefix[512];
        const int count = read(prefix, sizeof(prefix));
        if (count <= 0) return false;
        pending.assign(reinterpret_cast<char *>(prefix), count);
        hls = pending.find("#EXTM3U") != std::string::npos && pending.find("#EXT-X-") != std::string::npos;
        const bool is_playlist = hls || content_type.find("mpegurl") != std::string::npos ||
            content_type.find("scpls") != std::string::npos || pending.rfind("#EXTM3U", 0) == 0 || pending.rfind("[playlist]", 0) == 0;
        if (is_playlist) {
            std::string body = std::move(pending); pending.clear();
            while (body.size() < 65536) {
                const int n = read(prefix, sizeof(prefix));
                if (n == 0) break;
                if (n < 0 || !g_playback_requested.load() || g_sleep_requested.load() || g_scan_requested.load() || g_station.load() != slot || g_station_token.load() != station.id) return false;
                body.append(reinterpret_cast<char *>(prefix), n);
            }
            if (body.size() >= 65536) return false;
            hls = body.find("#EXT-X-") != std::string::npos;
            if (hls) { playlist = std::move(body); close(); return true; }
            address.clear();
            size_t begin = 0;
            while (begin < body.size()) {
                const size_t end = body.find('\n', begin);
                std::string line = body.substr(begin, end == std::string::npos ? end : end - begin);
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (line.rfind("File1=", 0) == 0) { address = line.substr(6); break; }
                if (!line.empty() && line[0] != '#' && line[0] != '[' &&
                    content_type.find("scpls") == std::string::npos && body.rfind("[playlist]", 0) != 0) { address = line; break; }
                if (end == std::string::npos) break;
                begin = end + 1;
            }
            if (address.empty()) return false;
            address = make_hls_segment_url(url, address); continue;
        }
        mp3 = content_type.find("audio/mpeg") != std::string::npos ||
            (content_type.find("aac") == std::string::npos && pending.rfind("ID3", 0) == 0);
        bool adts = false;
        for (size_t i = 0; i + 2 < pending.size(); ++i) {
            const auto *p = reinterpret_cast<const uint8_t *>(pending.data()) + i;
            if (p[0] == 0xff && (p[1] & 0xe0) == 0xe0) {
                adts = (p[1] & 0xf6) == 0xf0;
                if (!adts && (p[1] & 6)) mp3 = true;
                break;
            }
        }
        if (!mp3 && !adts && content_type.find("aac") == std::string::npos) return false;
        return true;
    }
    return false;
}
bool StreamInput::cancelled() const
{
    return !g_connected.load() || !g_playback_requested.load() || g_sleep_requested.load() ||
        g_scan_requested.load() || g_station.load() != selected_slot || g_station_token.load() != selected_id;
}
int StreamInput::read(uint8_t *buffer, size_t capacity)
{
    if (cancelled()) return -1;
    if (!pending.empty()) {
        const size_t n = std::min(capacity, pending.size());
        std::memcpy(buffer, pending.data(), n); pending.erase(0, n); return n;
    }
    const int64_t started = esp_timer_get_time();
    while (!cancelled() && esp_timer_get_time() - started < 30000000) {
        const int n = esp_http_client_read(client, reinterpret_cast<char *>(buffer), capacity);
        if (n < 0 && (n == -ESP_ERR_HTTP_EAGAIN || errno == EAGAIN || errno == ETIMEDOUT)) {
            vTaskDelay(pdMS_TO_TICKS(10)); continue;
        }
        if (n <= 0) return n;
        const size_t audio = icy.filter(buffer, n);
        if (!audio) continue;
        const int64_t elapsed = esp_timer_get_time() - started;
        if (elapsed > 0) {
            g_download_kbps.store(static_cast<int>(n * 8000ULL / elapsed));
            g_download_time.store(esp_timer_get_time());
        }
        return audio;
    }
    return -1;
}
}
