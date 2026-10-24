#pragma once

// Shared radio state and interfaces between firmware modules.
#include <array>
#include <atomic>
#include <cstdint>
#include <map>
#include <string>
#include <vector>
#include "bsp/esp32_p4_wifi6_touch_lcd_4b.h"
#include "esp_http_client.h"
#include "esp_http_server.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"

namespace p4radio {

inline constexpr char kTag[] = "p4radio";
inline constexpr char kApSsid[] = "p4Radio-Setup";
inline constexpr int kStationCount = 5;
inline constexpr int kMaxVolume = 10;
inline constexpr int kMinVolume = 1;
inline constexpr int kHttpPort = 80;
inline constexpr size_t kHlsSegmentMaxSize = 512 * 1024;
inline constexpr size_t kMaxHlsSegments = 24;

struct Station {
    const char *name;
    const char *playlist_url;
};

// BBC Radio 4 Extra occupies the repeated BBC4 slot in the original station list.
inline constexpr std::array<Station, kStationCount> kStations{{
    {"BBC Radio 1", "https://as-hls-ww-live.akamaized.net/pool_01505109/live/ww/bbc_radio_one/bbc_radio_one.isml/bbc_radio_one-audio=96000.norewind.m3u8"},
    {"BBC Radio 2", "https://as-hls-ww-live.akamaized.net/pool_74208725/live/ww/bbc_radio_two/bbc_radio_two.isml/bbc_radio_two-audio=96000.norewind.m3u8"},
    {"BBC Radio 4", "https://as-hls-ww-live.akamaized.net/pool_55057080/live/ww/bbc_radio_fourfm/bbc_radio_fourfm.isml/bbc_radio_fourfm-audio=96000.norewind.m3u8"},
    {"BBC Radio 4 Extra", "https://as-hls-ww-live.akamaized.net/pool_26173715/live/ww/bbc_radio_four_extra/bbc_radio_four_extra.isml/bbc_radio_four_extra-audio=96000.norewind.m3u8"},
    {"BBC Radio 5 Live", "https://as-hls-ww-live.akamaized.net/pool_89021708/live/ww/bbc_radio_five_live/bbc_radio_five_live.isml/bbc_radio_five_live-audio=96000.norewind.m3u8"},
}};

inline constexpr int64_t kJoinTimeoutUs = 30000000; // Association and DHCP.

extern std::atomic<int> g_station;
extern std::atomic<int> g_volume;
extern std::atomic<bool> g_connected;
extern std::atomic<bool> g_internet_probe_pending;
extern std::atomic<bool> g_portal_active;
extern std::atomic<bool> g_setup_submitted;
extern std::atomic<bool> g_playback_requested;
extern std::atomic<bool> g_sleep_requested;
extern std::atomic<bool> g_idle_ready;
extern std::atomic<bool> g_scan_requested;
extern esp_codec_dev_handle_t g_speaker;
extern std::atomic<int> g_ap_clients;
extern char g_hostname[33];
extern std::atomic<int> g_stream_kbps;
extern std::atomic<int> g_download_kbps;
extern std::atomic<int64_t> g_download_time;
extern std::map<std::string, int> g_url_bitrates;
extern portMUX_TYPE g_status_mux;
extern char g_saved_ssid[33];
extern char g_saved_password[65];
extern std::array<std::vector<std::string>, kStationCount> g_candidates;
extern std::array<size_t, kStationCount> g_candidate_index;

void set_status(const char *message);
std::string status_copy();
std::string hostname_copy();
bool valid_hostname(const std::string &name);
void save_preferences();
void load_preferences();
void save_wifi_credentials(const char *ssid, const char *password);
bool ensure_network_stack();
bool wifi_join_in_progress();
std::string cached_stream(int index);
void cache_stream(int index, const std::string &url);
std::string resolve_station_url(const Station &station, int index);
std::string fetch_hls_playlist(const std::string &url);
std::string make_hls_segment_url(const std::string &playlist_url, const std::string &segment);
size_t parse_hls_segments(const std::string &playlist, const std::string &playlist_url,
                          std::array<std::string, kMaxHlsSegments> &segments);
bool download_hls_segment(const std::string &url, uint8_t *buffer, size_t capacity, size_t &size);
bool extract_aac_from_mpeg_ts(const uint8_t *transport_stream, size_t transport_size,
                              uint8_t *aac_data, size_t aac_capacity, size_t &aac_size);
bool test_internet_connection();
void initialize_audio();
void playback_task(void *argument);
void build_ui();

} // namespace p4radio
