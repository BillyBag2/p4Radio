#include "radio.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include "esp_log.h"
#include "nvs.h"

namespace p4radio {

char g_status[96] = "Starting radio";

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

std::string hostname_copy()
{
    char name[sizeof(g_hostname)];
    portENTER_CRITICAL(&g_status_mux);
    std::memcpy(name, g_hostname, sizeof(name));
    portEXIT_CRITICAL(&g_status_mux);
    return name;
}

bool valid_hostname(const std::string &name)
{
    if (name.empty() || name.size() > 32 || name.front() == '-' || name.back() == '-') return false;
    for (unsigned char ch : name) if (!(ch >= 'a' && ch <= 'z') && !(ch >= '0' && ch <= '9') && ch != '-') return false;
    return true;
}

bool valid_max_bitrate(int kbps)
{
    return std::find(kStandardBitrates.begin(), kStandardBitrates.end(), kbps) != kStandardBitrates.end();
}

bool valid_display_skin(DisplaySkinId skin)
{
    return std::any_of(kDisplaySkins.begin(), kDisplaySkins.end(),
                       [skin](const auto &option) { return option.id == skin; });
}

const char *display_skin_name(DisplaySkinId skin)
{
    for (const auto &option : kDisplaySkins) if (option.id == skin) return option.name;
    return "Unknown";
}

void save_preferences()
{
    nvs_handle_t nvs;
    if (nvs_open("p4radio", NVS_READWRITE, &nvs) != ESP_OK) {
        return;
    }
    nvs_set_i32(nvs, "station", g_station.load());
    nvs_set_i32(nvs, "volume10", g_volume.load());
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
    if (nvs_get_i32(nvs, "station", &value) == ESP_OK && value >= 0 && value < kMaxStations) {
        g_station.store(value);
    }
    if (nvs_get_i32(nvs, "volume10", &value) != ESP_OK) {
        if (nvs_get_i32(nvs, "volume", &value) == ESP_OK) value = (value + 5) / 10;
        else value = 5;
    }
    {
        g_volume.store(std::clamp<int>(value, kMinVolume, kMaxVolume));
    }
    int32_t max_bitrate = 0;
    if (nvs_get_i32(nvs, "max_bitrate", &max_bitrate) == ESP_OK && valid_max_bitrate(max_bitrate))
        g_max_bitrate_kbps.store(max_bitrate);
    uint8_t display_flags = 0;
    if (nvs_get_u8(nvs, "display_flags", &display_flags) == ESP_OK) {
        g_show_volume_bar.store((display_flags & 1) != 0);
        g_show_buffer_bar.store((display_flags & 2) != 0);
        g_show_power_meter.store((display_flags & 4) != 0);
    }
    uint8_t skin = 0;
    if (nvs_get_u8(nvs, "skin", &skin) == ESP_OK && valid_display_skin(static_cast<DisplaySkinId>(skin)))
        g_display_skin.store(static_cast<DisplaySkinId>(skin));
    size_t name_size = sizeof(g_hostname);
    char name[sizeof(g_hostname)] = {};
    if (nvs_get_str(nvs, "hostname", name, &name_size) == ESP_OK && valid_hostname(name))
        std::snprintf(g_hostname, sizeof(g_hostname), "%s", name);
    size_t size = sizeof(g_saved_ssid);
    nvs_get_str(nvs, "ssid", g_saved_ssid, &size);
    size = sizeof(g_saved_password);
    nvs_get_str(nvs, "password", g_saved_password, &size);
    nvs_close(nvs);
}

esp_err_t save_web_settings(const std::string &hostname, bool volume_bar, bool buffer_bar, int max_bitrate,
                            bool power_meter, DisplaySkinId skin)
{
    if (!valid_hostname(hostname) || !valid_max_bitrate(max_bitrate) ||
        !valid_display_skin(skin)) return ESP_ERR_INVALID_ARG;
    nvs_handle_t handle;
    esp_err_t result = nvs_open("p4radio", NVS_READWRITE, &handle);
    if (result != ESP_OK) return result;
    result = nvs_set_str(handle, "hostname", hostname.c_str());
    if (result == ESP_OK) result = nvs_set_u8(handle, "display_flags", (volume_bar ? 1 : 0) | (buffer_bar ? 2 : 0) | (power_meter ? 4 : 0));
    if (result == ESP_OK) result = nvs_set_i32(handle, "max_bitrate", max_bitrate);
    if (result == ESP_OK) result = nvs_set_u8(handle, "skin", static_cast<uint8_t>(skin));
    if (result == ESP_OK) result = nvs_commit(handle);
    nvs_close(handle);
    if (result != ESP_OK) return result;
    portENTER_CRITICAL(&g_status_mux);
    std::snprintf(g_hostname, sizeof(g_hostname), "%s", hostname.c_str());
    portEXIT_CRITICAL(&g_status_mux);
    g_show_volume_bar.store(volume_bar);
    g_show_buffer_bar.store(buffer_bar);
    g_show_power_meter.store(power_meter);
    g_display_skin.store(skin);
    ESP_LOGI(kTag, "Saved display skin: %s", display_skin_name(skin));
    if (g_max_bitrate_kbps.exchange(max_bitrate) != max_bitrate) g_scan_requested.store(true);
    return ESP_OK;
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


} // namespace p4radio
