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
    if (nvs_get_i32(nvs, "station", &value) == ESP_OK && value >= 0 && value < kStationCount) {
        g_station.store(value);
    }
    if (nvs_get_i32(nvs, "volume10", &value) != ESP_OK) {
        if (nvs_get_i32(nvs, "volume", &value) == ESP_OK) value = (value + 5) / 10;
        else value = 5;
    }
    {
        g_volume.store(std::clamp<int>(value, kMinVolume, kMaxVolume));
    }
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
