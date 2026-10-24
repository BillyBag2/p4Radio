#include "radio.hpp"

namespace p4radio {

std::atomic<int> g_station{0};
std::atomic<int> g_volume{5};
std::atomic<bool> g_connected{false};
std::atomic<bool> g_internet_probe_pending{false};
std::atomic<bool> g_portal_active{false};
std::atomic<bool> g_setup_submitted{false};
std::atomic<bool> g_playback_requested{true};
std::atomic<bool> g_sleep_requested{false};
std::atomic<bool> g_idle_ready{false};
std::atomic<bool> g_scan_requested{false};
esp_codec_dev_handle_t g_speaker = nullptr;
std::atomic<int> g_ap_clients{0};
char g_hostname[33] = "p4radio";
std::atomic<int> g_stream_kbps{0};
std::atomic<int> g_download_kbps{0};
std::atomic<int64_t> g_download_time{0};
std::map<std::string, int> g_url_bitrates;
portMUX_TYPE g_status_mux = portMUX_INITIALIZER_UNLOCKED;
char g_saved_ssid[33] = {};
char g_saved_password[65] = {};
std::array<std::vector<std::string>, kStationCount> g_candidates;
std::array<size_t, kStationCount> g_candidate_index{};

} // namespace p4radio
