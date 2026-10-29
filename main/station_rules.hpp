#pragma once
#include <span>
#include <string_view>
#include <cstdint>

namespace p4radio::station_rules {
constexpr bool bounded_json(std::string_view input)
{
    int depth = 0;
    bool quoted = false, escaped = false;
    for (char ch : input) {
        if (quoted) {
            if (escaped) escaped = false;
            else if (ch == '\\') escaped = true;
            else if (ch == '"') quoted = false;
        } else if (ch == '"') quoted = true;
        else if (ch == '{' || ch == '[') { if (++depth > 8) return false; }
        else if (ch == '}' || ch == ']') { if (--depth < 0) return false; }
    }
    return !quoted && depth == 0;
}

constexpr int adjacent(std::span<const bool> enabled, int current, int direction)
{
    const int count = static_cast<int>(enabled.size());
    if (!count) return -1;
    if (current < 0 || current >= count) current = direction < 0 ? 0 : count - 1;
    for (int step = 1; step <= count; ++step) {
        const int slot = (current + (direction < 0 ? -step : step) + count) % count;
        if (enabled[slot]) return slot;
    }
    return -1;
}

constexpr bool valid_name(std::string_view name)
{
    if (name.empty() || name.size() > 96) return false;
    bool visible = false;
    for (unsigned char ch : name) {
        if (ch < 32 || ch == 127) return false;
        if (ch != ' ') visible = true;
    }
    return visible;
}

constexpr bool valid_url(std::string_view url)
{
    const size_t start = url.starts_with("https://") ? 8 : url.starts_with("http://") ? 7 : 0;
    if (!start || url.size() <= start || url.size() > 1024) return false;
    const size_t end = url.find_first_of("/?#", start);
    const auto host = url.substr(start, end == std::string_view::npos ? end : end - start);
    if (host.empty() || host.find('@') != std::string_view::npos) return false;
    for (unsigned char ch : url) if (ch <= 32 || ch == 127 || ch == '\\') return false;
    return true;
}

constexpr bool valid_uuid(std::string_view value)
{
    if (value.size() != 36) return false;
    for (size_t i = 0; i < value.size(); ++i) {
        const char ch = value[i];
        if (i == 8 || i == 13 || i == 18 || i == 23) { if (ch != '-') return false; }
        else if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F'))) return false;
    }
    return true;
}

constexpr bool cache_owner(uint32_t station_id, bool builtin, int slot, bool has_tag, uint32_t tag)
{
    return station_id && (has_tag ? station_id == tag : builtin && station_id == static_cast<uint32_t>(slot + 1));
}
} // namespace p4radio::station_rules
