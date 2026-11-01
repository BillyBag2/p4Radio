#include "station_catalog.hpp"
#include "station_rules.hpp"
#include "button_audio.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>
#include "esp_log.h"
#include "nvs.h"
#include "freertos/semphr.h"

namespace p4radio {
namespace {
constexpr size_t kCatalogBytes = 6144;
struct Record {
    char name[97]{};
    char url[1025]{};
    char uuid[37]{};
    uint32_t id = 0;
    int bitrate = 0;
    bool enabled = false;
    bool hls = true;
};
struct Catalog { std::array<Record, kMaxStations> records{}; uint32_t next = kStationCount + 1; };
Catalog catalog;
portMUX_TYPE catalog_mux = portMUX_INITIALIZER_UNLOCKED;
StaticSemaphore_t writer_state;
SemaphoreHandle_t writer = nullptr;
using Json = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>;

std::unique_ptr<Catalog> snapshot()
{
    std::unique_ptr<Catalog> copy(new (std::nothrow) Catalog);
    if (copy) {
        portENTER_CRITICAL(&catalog_mux);
        *copy = catalog;
        portEXIT_CRITICAL(&catalog_mux);
    }
    return copy;
}

void reset_record(Record &record)
{
    std::fill(std::begin(record.name), std::end(record.name), '\0');
    std::fill(std::begin(record.url), std::end(record.url), '\0');
    std::fill(std::begin(record.uuid), std::end(record.uuid), '\0');
    record.id = 0;
    record.bitrate = 0;
    record.enabled = false;
    record.hls = true;
}

void defaults(Catalog &value)
{
    for (int i = 0; i < kStationCount; ++i) {
        auto &record = value.records[i];
        std::snprintf(record.name, sizeof(record.name), "%s", kStations[i].name);
        std::snprintf(record.url, sizeof(record.url), "%s", kStations[i].playlist_url);
        record.id = i + 1;
        record.enabled = true;
        record.bitrate = 96;
    }
}

void publish(const Catalog &value)
{
    portENTER_CRITICAL(&catalog_mux);
    catalog = value;
    const int selected = g_station.load();
    if (selected < 0 || selected >= kMaxStations || !catalog.records[selected].enabled) {
        std::array<bool, kMaxStations> enabled{};
        for (int i = 0; i < kMaxStations; ++i) enabled[i] = catalog.records[i].id && catalog.records[i].enabled;
        g_station.store(station_rules::adjacent(enabled, selected, 1));
    }
    const int active = g_station.load();
    g_station_token.store(active >= 0 ? catalog.records[active].id : 0);
    portEXIT_CRITICAL(&catalog_mux);
}

const char *text(const cJSON *object, const char *key)
{
    const cJSON *value = cJSON_GetObjectItemCaseSensitive(object, key);
    return cJSON_IsString(value) ? value->valuestring : "";
}

bool number(const cJSON *object, const char *key, uint32_t &result)
{
    const cJSON *value = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!cJSON_IsNumber(value) || value->valuedouble < 0 || value->valuedouble > UINT32_MAX ||
        std::floor(value->valuedouble) != value->valuedouble) return false;
    result = static_cast<uint32_t>(value->valuedouble);
    return true;
}

cJSON *serialize(const Catalog &value, bool web)
{
    Json root(cJSON_CreateObject(), cJSON_Delete);
    if (!root || !cJSON_AddNumberToObject(root.get(), "version", 1) ||
        !cJSON_AddNumberToObject(root.get(), "next", value.next)) return nullptr;
    cJSON *items = cJSON_AddArrayToObject(root.get(), "stations");
    if (!items) return nullptr;
    for (int slot = 0; slot < kMaxStations; ++slot) {
        const auto &record = value.records[slot];
        if (!record.id) continue;
        cJSON *item = cJSON_CreateObject();
        if (!item) return nullptr;
        cJSON_AddItemToArray(items, item);
        if (!cJSON_AddNumberToObject(item, "slot", slot) || !cJSON_AddNumberToObject(item, "id", record.id) ||
            !cJSON_AddBoolToObject(item, "enabled", record.enabled)) return nullptr;
        if (web || slot >= kStationCount) {
            if (!cJSON_AddStringToObject(item, "name", record.name) || !cJSON_AddStringToObject(item, "url", record.url) ||
                !cJSON_AddStringToObject(item, "uuid", record.uuid) || !cJSON_AddNumberToObject(item, "bitrate", record.bitrate) ||
                !cJSON_AddBoolToObject(item, "builtin", slot < kStationCount) ||
                !cJSON_AddBoolToObject(item, "hls", record.hls)) return nullptr;
        }
    }
    if (web && (!cJSON_AddNumberToObject(root.get(), "selected", g_station.load()) ||
        !cJSON_AddNumberToObject(root.get(), "capacity", kMaxStations) ||
        !cJSON_AddNumberToObject(root.get(), "max_bitrate", g_max_bitrate_kbps.load()))) return nullptr;
    return root.release();
}

bool decode(const cJSON *root, Catalog &value)
{
    uint32_t version;
    if (!number(root, "version", version) || version != 1 || !number(root, "next", value.next) ||
        value.next < kStationCount + 1) return false;
    const cJSON *items = cJSON_GetObjectItemCaseSensitive(root, "stations");
    if (!cJSON_IsArray(items) || cJSON_GetArraySize(items) > kMaxStations) return false;
    std::array<bool, kMaxStations> used{};
    const cJSON *item;
    cJSON_ArrayForEach(item, items) {
        uint32_t slot, id;
        const auto *enabled = cJSON_GetObjectItemCaseSensitive(item, "enabled");
        if (!number(item, "slot", slot) || slot >= kMaxStations || used[slot] ||
            !number(item, "id", id) || !id || id >= value.next || !cJSON_IsBool(enabled)) return false;
        used[slot] = true;
        auto &record = value.records[slot];
        if (slot < kStationCount) {
            if (id != slot + 1) return false;
        } else {
            uint32_t bitrate;
            if (id <= kStationCount || !station_rules::valid_name(text(item, "name")) ||
                !station_rules::valid_url(text(item, "url")) || !station_rules::valid_uuid(text(item, "uuid")) ||
                !number(item, "bitrate", bitrate) || bitrate > 10000) return false;
            for (int i = 0; i < kMaxStations; ++i) {
                if (i != static_cast<int>(slot) && value.records[i].id == id) return false;
            }
            std::snprintf(record.name, sizeof(record.name), "%s", text(item, "name"));
            std::snprintf(record.url, sizeof(record.url), "%s", text(item, "url"));
            std::snprintf(record.uuid, sizeof(record.uuid), "%s", text(item, "uuid"));
            record.id = id;
            record.bitrate = bitrate;
            const auto *hls = cJSON_GetObjectItemCaseSensitive(item, "hls");
            if (hls && !cJSON_IsBool(hls)) return false;
            record.hls = !hls || cJSON_IsTrue(hls);
        }
        record.enabled = cJSON_IsTrue(enabled);
    }
    return true;
}
} // namespace

void load_station_catalog()
{
    writer = xSemaphoreCreateMutexStatic(&writer_state);
    std::unique_ptr<Catalog> value(new (std::nothrow) Catalog);
    if (!value) {
        defaults(catalog);
        g_station.store(0);
        g_station_token.store(1);
        ESP_LOGE(kTag, "Cannot allocate station catalog; using presets");
        return;
    }
    defaults(*value);
    nvs_handle_t handle;
    if (nvs_open("p4radio", NVS_READONLY, &handle) == ESP_OK) {
        size_t size = 0;
        if (nvs_get_blob(handle, "stations", nullptr, &size) == ESP_OK && size > 1 && size <= kCatalogBytes) {
            std::string data(size, '\0');
            if (nvs_get_blob(handle, "stations", data.data(), &size) == ESP_OK && data.back() == '\0' &&
                station_rules::bounded_json(data)) {
                const char *end = nullptr;
                Json root(cJSON_ParseWithLengthOpts(data.c_str(), data.size(), &end, true), cJSON_Delete);
                if (!root || end != data.c_str() + data.size() - 1 || !decode(root.get(), *value)) {
                    for (auto &record : value->records) reset_record(record);
                    value->next = kStationCount + 1;
                    defaults(*value);
                    ESP_LOGW(kTag, "Invalid saved station catalog; using presets");
                }
            }
        }
        nvs_close(handle);
    }
    publish(*value);
}

Station station_copy(int slot)
{
    Record record;
    if (slot >= 0 && slot < kMaxStations) {
        portENTER_CRITICAL(&catalog_mux);
        record = catalog.records[slot];
        portEXIT_CRITICAL(&catalog_mux);
    }
    if (!record.id) return {"No stations enabled", {}, {}, 0, 0, false, false};
    return {record.name, record.url, record.uuid, record.id, record.bitrate, slot < kStationCount, record.enabled, record.hls};
}

std::string selected_station_name()
{
    char name[97]{};
    portENTER_CRITICAL(&catalog_mux);
    const int slot = g_station.load();
    if (slot >= 0 && slot < kMaxStations && catalog.records[slot].id)
        std::memcpy(name, catalog.records[slot].name, sizeof(name));
    portEXIT_CRITICAL(&catalog_mux);
    return name[0] ? std::string(name) : "No stations enabled";
}

int select_adjacent_station(int direction)
{
    portENTER_CRITICAL(&catalog_mux);
    std::array<bool, kMaxStations> enabled{};
    for (int i = 0; i < kMaxStations; ++i) enabled[i] = catalog.records[i].id && catalog.records[i].enabled;
    const int selected = station_rules::adjacent(enabled, g_station.load(), direction);
    g_station.store(selected);
    g_station_token.store(selected >= 0 ? catalog.records[selected].id : 0);
    portEXIT_CRITICAL(&catalog_mux);
    return selected;
}

int station_dial_position()
{
    portENTER_CRITICAL(&catalog_mux);
    int count = 0, rank = 0;
    const int selected = g_station.load();
    for (int i = 0; i < kMaxStations; ++i) if (catalog.records[i].id && catalog.records[i].enabled) {
        if (i < selected) ++rank;
        ++count;
    }
    portEXIT_CRITICAL(&catalog_mux);
    return count > 1 ? rank * 516 / (count - 1) : 0;
}

cJSON *station_catalog_json()
{
    auto value = snapshot();
    return value ? serialize(*value, true) : nullptr;
}

esp_err_t update_station_catalog(const cJSON *request, std::string &message)
{
    if (!writer) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(writer, portMAX_DELAY);
    struct Unlock { ~Unlock() { xSemaphoreGive(writer); } } unlock;
    auto value = snapshot();
    if (!value) return ESP_ERR_NO_MEM;
    const std::string action = text(request, "action");
    if (action == "add") {
        const std::string name = text(request, "name"), url = text(request, "url");
        std::string uuid = text(request, "uuid");
        for (char &ch : uuid) if (ch >= 'A' && ch <= 'F') ch += 'a' - 'A';
        uint32_t bitrate;
        const std::string codec = text(request, "codec");
        if (!station_rules::valid_name(name) || !station_rules::valid_url(url) || !station_rules::valid_uuid(uuid) ||
            !number(request, "bitrate", bitrate) || bitrate > 10000 ||
            !cJSON_IsBool(cJSON_GetObjectItemCaseSensitive(request, "hls")) ||
            (codec.find("AAC") == std::string::npos &&
             !(codec == "MP3" && cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(request, "hls"))))) {
            message = "Choose an AAC HLS, direct AAC or MP3 station with a valid name, URL and station UUID";
            return ESP_ERR_INVALID_ARG;
        }
        int free = -1;
        for (int i = 0; i < kMaxStations; ++i) {
            const auto &record = value->records[i];
            if (record.id && (uuid == record.uuid || url == record.url)) {
                message = "Station is already saved; re-enable it in My stations if muted";
                return ESP_ERR_INVALID_STATE;
            }
            if (!record.id && free < 0) free = i;
        }
        if (free < 0 || value->next == UINT32_MAX) {
            message = "Station list is full; remove an added station first";
            return ESP_ERR_NO_MEM;
        }
        auto &record = value->records[free];
        std::snprintf(record.name, sizeof(record.name), "%s", name.c_str());
        std::snprintf(record.url, sizeof(record.url), "%s", url.c_str());
        std::snprintf(record.uuid, sizeof(record.uuid), "%s", uuid.c_str());
        record.id = value->next++;
        record.enabled = true;
        record.bitrate = bitrate;
        record.hls = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(request, "hls"));
    } else {
        uint32_t slot, id;
        if (!number(request, "slot", slot) || slot >= kMaxStations || !number(request, "id", id) ||
            !id || value->records[slot].id != id) {
            message = "The station list has changed; refresh it and try again";
            return ESP_ERR_INVALID_STATE;
        }
        if (action == "mute") value->records[slot].enabled = false;
        else if (action == "unmute") value->records[slot].enabled = true;
        else if (action == "remove" && slot >= kStationCount) reset_record(value->records[slot]);
        else { message = "Presets can be muted; only added stations can be removed"; return ESP_ERR_INVALID_ARG; }
    }
    Json root(serialize(*value, false), cJSON_Delete);
    if (!root) return ESP_ERR_NO_MEM;
    std::unique_ptr<char, decltype(&cJSON_free)> encoded(cJSON_PrintUnformatted(root.get()), cJSON_free);
    if (!encoded) return ESP_ERR_NO_MEM;
    if (std::strlen(encoded.get()) + 1 > kCatalogBytes) {
        message = "Station storage is full; remove an added station before adding more";
        return ESP_ERR_NO_MEM;
    }
    nvs_handle_t handle;
    esp_err_t result = nvs_open("p4radio", NVS_READWRITE, &handle);
    if (result == ESP_OK) {
        // A blob spans NVS pages; nvs_set_str is limited to one 4000-byte page.
        result = nvs_set_blob(handle, "stations", encoded.get(), std::strlen(encoded.get()) + 1);
        if (result == ESP_OK) result = nvs_commit(handle);
        nvs_close(handle);
    }
    if (result != ESP_OK) {
        message = result == ESP_ERR_NVS_NOT_ENOUGH_SPACE ?
            "Station storage is full. Press TUNE on the radio to clear cached streams, then retry" :
            "Could not save stations; NVS storage may be full";
        return result;
    }
    publish(*value);
    request_preferences_save();
    message = "Stations saved";
    return ESP_OK;
}
} // namespace p4radio
