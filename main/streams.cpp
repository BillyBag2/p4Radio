#include "radio.hpp"
#include "radio_browser_dns.hpp"
#include "station_rules.hpp"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include "cJSON.h"
#include "esp_timer.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "nvs.h"

namespace p4radio {



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


int stream_url_bitrate(const std::string &url)
{
    // BBC media URLs explicitly identify nominal audio bitrate (not TS overhead).
    for (const char *marker : {"audio=", "audio%3d", "audio%3D"}) {
        const size_t position = url.find(marker);
        if (position != std::string::npos) {
            const long rate = std::strtol(url.c_str() + position + std::strlen(marker), nullptr, 10);
            if (rate > 0 && rate < 10000000) return static_cast<int>((rate + 999) / 1000);
        }
    }
    return 0;
}

std::string cached_stream(int index)
{
    nvs_handle_t handle;
    if (nvs_open("streams96", NVS_READONLY, &handle) != ESP_OK) return {};
    const Station station = station_copy(index);
    char owner_key[16];
    std::snprintf(owner_key, sizeof(owner_key), "owner%d", index);
    uint32_t owner = 0;
    const bool has_owner = nvs_get_u32(handle, owner_key, &owner) == ESP_OK;
    if (!station_rules::cache_owner(station.id, station.builtin, index, has_owner, owner)) {
        nvs_close(handle);
        return {};
    }
    const int limit = g_max_bitrate_kbps.load();
    if (limit > 0) {
        char cap_key[16];
        std::snprintf(cap_key, sizeof(cap_key), "cap%d", index);
        int32_t saved_limit = -1;
        if (nvs_get_i32(handle, cap_key, &saved_limit) != ESP_OK || saved_limit != limit) {
            nvs_close(handle);
            return {};
        }
    }
    size_t size = 0;
    std::string value;
    char key[16];
    std::snprintf(key, sizeof(key), "station%d", index);
    if (nvs_get_str(handle, key, nullptr, &size) == ESP_OK && size > 1 && size <= 2048) {
        value.resize(size);
        if (nvs_get_str(handle, key, value.data(), &size) == ESP_OK) value.resize(size - 1);
        else value.clear();
    }
    nvs_close(handle);
    return value;
}

void cache_stream(int index, const std::string &url, uint32_t station_id)
{
    if (!station_id || station_copy(index).id != station_id) return;
    if (!url.empty() && g_scan_requested.load()) return;
    // NVS keys have a 15-character limit; use a separate key per preset below.
    char key[16];
    std::snprintf(key, sizeof(key), "station%d", index);
    nvs_handle_t handle;
    if (nvs_open("streams96", NVS_READWRITE, &handle) != ESP_OK) return;
    esp_err_t result = url.empty() ? nvs_erase_key(handle, key) : nvs_set_str(handle, key, url.c_str());
    if (result == ESP_OK && !url.empty()) {
        char cap_key[16];
        std::snprintf(cap_key, sizeof(cap_key), "cap%d", index);
        result = nvs_set_i32(handle, cap_key, g_max_bitrate_kbps.load());
        if (result == ESP_OK) {
            char owner_key[16];
            std::snprintf(owner_key, sizeof(owner_key), "owner%d", index);
            result = nvs_set_u32(handle, owner_key, station_id);
        }
    }
    if (result == ESP_OK) result = nvs_commit(handle);
    if (result != ESP_OK && result != ESP_ERR_NVS_NOT_FOUND) ESP_LOGW(kTag, "Stream cache: %s", esp_err_to_name(result));
    nvs_close(handle);
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

std::string resolve_station_url(const Station &station, int index)
{
    if (index < 0 || index >= kMaxStations || !station.id || !station.enabled) return {};
    const int limit = g_max_bitrate_kbps.load();
    auto &urls = g_candidates[index];
    static std::array<uint32_t, kMaxStations> owners{}; // Playback task only.
    if (owners[index] != station.id) {
        urls.clear();
        g_candidate_index[index] = 0;
        owners[index] = station.id;
    }
    if (urls.empty()) {
        const std::string saved = cached_stream(index);
        if (!saved.empty()) urls.push_back(saved);
        // Query only when needed: a working cached stream starts without directory access.
    }
    if (g_candidate_index[index] < urls.size()) return urls[g_candidate_index[index]];
    urls.clear();
    g_candidate_index[index] = 0;
    set_status((std::string("Searching for ") + station.name).c_str());
    std::string encoded;
    for (const unsigned char ch : std::string(station.name)) {
        if (std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.') encoded += ch;
        else {
            char escape[4];
            std::snprintf(escape, sizeof(escape), "%%%02X", ch);
            encoded += escape;
        }
    }
    const auto cancelled = [index, station_id = station.id] {
        return !g_connected.load() || !g_playback_requested.load() || g_sleep_requested.load() ||
            g_scan_requested.load() || g_station.load() != index || g_station_token.load() != station_id;
    };
    std::map<std::string, int> bitrates; // Search-local: removed stations do not accumulate metadata.
    const auto directory_servers = discover_radio_browser_servers();
    if (cancelled()) return {};
    for (const std::string &server : directory_servers) {
        if (cancelled()) return {};
        const std::string query = server + (station.uuid.empty() ?
            "/json/stations/search?name=" + encoded +
            "&nameExact=true&hidebroken=true&limit=50&order=bitrate&reverse=false" :
            "/json/stations/byuuid?uuids=" + station.uuid);
        int status = 0;
        esp_err_t error = ESP_OK;
        auto client = open_http_get(query, status, error);
        if (!client) {
            ESP_LOGW(kTag, "Radio Browser request failed: %s (%s, HTTP %d)", server.c_str(), esp_err_to_name(error), status);
            continue;
        }
        const std::string body = read_http_body(client, 65536);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        cJSON *root = cJSON_Parse(body.c_str());
        if (cJSON_IsArray(root)) {
            cJSON *entry = nullptr;
            cJSON_ArrayForEach(entry, root) {
                const auto *hls = cJSON_GetObjectItemCaseSensitive(entry, "hls");
                const auto *codec = cJSON_GetObjectItemCaseSensitive(entry, "codec");
                const auto *url = cJSON_GetObjectItemCaseSensitive(entry, "url_resolved");
                if (!cJSON_IsNumber(hls) || hls->valueint != 1 || !cJSON_IsString(codec) ||
                    std::string(codec->valuestring).find("AAC") == std::string::npos ||
                    !cJSON_IsString(url)) continue;
                const std::string candidate = url->valuestring;
                const auto *rate = cJSON_GetObjectItemCaseSensitive(entry, "bitrate");
                const int kbps = cJSON_IsNumber(rate) ? rate->valueint : 0;
                if (limit > 0 && (kbps <= 0 || kbps > limit)) continue;
                if (candidate.size() <= 2047 && (candidate.rfind("https://", 0) == 0 || candidate.rfind("http://", 0) == 0) &&
                    std::find(urls.begin(), urls.end(), candidate) == urls.end()) {
                    urls.push_back(candidate);
                    bitrates[candidate] = kbps;
                }
            }
        }
        cJSON_Delete(root);
        if (!urls.empty()) break;
    }
    // Include explicit BBC variants so a higher ceiling can select higher
    // quality even if the directory only lists the worldwide 96 kbit/s feed.
    if (!station.builtin && (limit == 0 || (station.bitrate > 0 && station.bitrate <= limit))) {
        if (std::find(urls.begin(), urls.end(), station.playlist_url) == urls.end()) urls.push_back(station.playlist_url);
        bitrates[station.playlist_url] = station.bitrate;
    }
    if (station.builtin) for (int bitrate : {320, 128, 96, 48}) {
        if (limit > 0 && bitrate > limit) continue;
        std::string fallback = station.playlist_url;
        const size_t marker = fallback.find("audio=96000");
        if (marker == std::string::npos) continue;
        fallback.replace(marker, std::strlen("audio=96000"), "audio=" + std::to_string(bitrate * 1000));
        if (bitrate > 96) {
            const size_t host = fallback.find("as-hls-ww-live");
            if (host != std::string::npos) fallback.replace(host, std::strlen("as-hls-ww-live"), "as-hls-uk-live");
            const size_t path = fallback.find("/live/ww/");
            if (path != std::string::npos) fallback.replace(path, std::strlen("/live/ww/"), "/live/uk/");
        }
        if (std::find(urls.begin(), urls.end(), fallback) == urls.end()) urls.push_back(fallback);
        bitrates[fallback] = bitrate;
    }
    std::stable_sort(urls.begin(), urls.end(), [&bitrates](const std::string &a, const std::string &b) {
        const int ar = bitrates[a], br = bitrates[b];
        return ar > br; // Highest available rate, including Unlimited.
    });
    ESP_LOGI(kTag, "%s: trying %u stream candidates", station.name.c_str(), static_cast<unsigned>(urls.size()));
    return urls.empty() ? std::string{} : urls.front();
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
    const int64_t started = esp_timer_get_time();
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
    const int64_t elapsed = esp_timer_get_time() - started;
    if (size && elapsed > 0) {
        g_download_kbps.store(static_cast<int>(static_cast<uint64_t>(size) * 8000 / elapsed));
        g_download_time.store(esp_timer_get_time());
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


} // namespace p4radio
