#pragma once

#include <algorithm>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace p4radio {

// Private directory-discovery interface; no persistent server list or fixed hosts.
std::vector<std::string> discover_radio_browser_servers(bool for_playback = true);

namespace radio_browser_dns {
inline constexpr char kService[] = "_api._tcp.radio-browser.info";
struct Server {
    std::string host;
    uint16_t port;
    constexpr bool operator==(const Server &) const = default;
};
struct Reply {
    bool valid = false;
    bool truncated = false;
    std::vector<Server> servers;
};

constexpr uint16_t word(std::span<const uint8_t> packet, size_t offset)
{
    return (static_cast<uint16_t>(packet[offset]) << 8) | packet[offset + 1];
}

// DNS names can contain compression pointers, including inside SRV RDATA.
// Bound both packet reads and pointer traversal to reject loops/malformed data.
constexpr bool read_name(std::span<const uint8_t> packet, size_t &offset, std::string &name)
{
    name.clear();
    size_t cursor = offset;
    bool jumped = false;
    for (size_t steps = 0; steps < 128 && cursor < packet.size(); ++steps) {
        const uint8_t length = packet[cursor++];
        if ((length & 0xc0) == 0xc0) {
            if (cursor >= packet.size()) return false;
            const size_t target = ((length & 0x3f) << 8) | packet[cursor++];
            if (!jumped) offset = cursor;
            jumped = true;
            cursor = target;
            continue;
        }
        if (length > 63 || cursor + length > packet.size()) return false;
        if (!length) {
            if (!jumped) offset = cursor;
            return true;
        }
        if (!name.empty()) name += '.';
        for (size_t i = 0; i < length; ++i) {
            char ch = static_cast<char>(packet[cursor++]);
            if (ch >= 'A' && ch <= 'Z') ch += 'a' - 'A';
            name += ch;
        }
        if (name.size() > 253) return false;
        if (!jumped) offset = cursor;
    }
    return false;
}

constexpr bool hostname_valid(const std::string &host)
{
    size_t label = 0;
    char previous = 0;
    for (char ch : host) {
        if (ch == '.') {
            if (!label || previous == '-') return false;
            label = 0;
        } else {
            if (!((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '-') ||
                (!label && ch == '-') || ++label > 63) return false;
        }
        previous = ch;
    }
    return label && previous != '-';
}

constexpr Reply parse_reply(std::span<const uint8_t> packet, uint16_t id)
{
    if (packet.size() < 12 || word(packet, 0) != id ||
        (word(packet, 2) & 0xf80f) != 0x8000 || word(packet, 4) != 1) return {};
    size_t offset = 12;
    std::string name;
    if (!read_name(packet, offset, name) || name != kService || offset + 4 > packet.size() ||
        word(packet, offset) != 33 || word(packet, offset + 2) != 1) return {};
    offset += 4;
    Reply result{true, (word(packet, 2) & 0x0200) != 0, {}};
    if (result.truncated) return result; // Re-query over TCP before using any answer.
    const uint16_t answers = word(packet, 6);
    for (uint16_t i = 0; i < answers; ++i) {
        if (!read_name(packet, offset, name) || offset + 10 > packet.size()) return {};
        const uint16_t type = word(packet, offset), klass = word(packet, offset + 2);
        const size_t length = word(packet, offset + 8);
        offset += 10;
        const size_t end = offset + length;
        if (end > packet.size()) return {};
        if (name == kService && type == 33 && klass == 1) {
            if (length < 7) return {};
            const uint16_t port = word(packet, offset + 4);
            size_t target = offset + 6;
            std::string host;
            if (!read_name(packet, target, host) || target != end) return {};
            // A root target means the service is unavailable; reject URL syntax.
            if (port && hostname_valid(host)) {
                Server server{host, port};
                if (std::find(result.servers.begin(), result.servers.end(), server) == result.servers.end())
                    result.servers.push_back(server);
            }
        }
        offset = end;
    }
    return result;
}
} // namespace radio_browser_dns
} // namespace p4radio
