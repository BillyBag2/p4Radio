// Compile-only regression checks against the actual wire parser. No board or
// host executable is needed: use the ESP-IDF C++ compiler with -std=gnu++26
// -fsyntax-only -I main tools/check_radio_browser_dns.cpp.
#include "radio_browser_dns.hpp"

using namespace p4radio::radio_browser_dns;

constexpr void append_word(std::vector<uint8_t> &packet, uint16_t value)
{
    packet.push_back(value >> 8);
    packet.push_back(value & 255);
}

constexpr void append_name(std::vector<uint8_t> &packet, const std::string &name)
{
    size_t start = 0;
    while (start < name.size()) {
        const size_t dot = name.find('.', start);
        const size_t end = dot == std::string::npos ? name.size() : dot;
        packet.push_back(end - start);
        for (size_t i = start; i < end; ++i) packet.push_back(name[i]);
        start = end + 1;
    }
    packet.push_back(0);
}

constexpr std::vector<uint8_t> response(const std::vector<Server> &servers)
{
    std::vector<uint8_t> packet{0x12, 0x34, 0x81, 0x80, 0, 1, 0, 0, 0, 0, 0, 0};
    packet[7] = servers.size();
    append_name(packet, kService);
    append_word(packet, 33);
    append_word(packet, 1);
    for (const auto &server : servers) {
        packet.insert(packet.end(), {0xc0, 0x0c}); // Compressed answer owner.
        append_word(packet, 33);
        append_word(packet, 1);
        packet.insert(packet.end(), {0, 0, 0, 60});
        std::vector<uint8_t> target;
        append_name(target, server.host);
        append_word(packet, target.size() + 6);
        append_word(packet, 1); // priority
        append_word(packet, 1); // weight
        append_word(packet, server.port);
        packet.insert(packet.end(), target.begin(), target.end());
    }
    return packet;
}

static_assert([] {
    auto reply = parse_reply(response({{"de1.api.radio-browser.info", 443}, {"nl1.api.radio-browser.info", 8443}}), 0x1234);
    return reply.valid && !reply.truncated && reply.servers.size() == 2 &&
        reply.servers[0].host == "de1.api.radio-browser.info" && reply.servers[1].port == 8443;
}());

static_assert([] {
    auto reply = parse_reply(response({{"DE1.API.RADIO-BROWSER.INFO", 443}, {"de1.api.radio-browser.info", 443}}), 0x1234);
    return reply.valid && reply.servers.size() == 1;
}());

static_assert([] {
    auto packet = response({{"de1.api.radio-browser.info", 443}});
    const size_t answer = response({}).size();
    packet.resize(answer + 18); // Retain owner, header, priority/weight/port.
    packet.insert(packet.end(), {3, 'd', 'e', '1', 3, 'a', 'p', 'i', 0xc0, 22});
    packet[answer + 10] = 0;
    packet[answer + 11] = 16; // compressed target + six SRV bytes
    auto reply = parse_reply(packet, 0x1234);
    return reply.valid && reply.servers.size() == 1 && reply.servers[0].host == "de1.api.radio-browser.info";
}());

static_assert([] {
    auto packet = response({});
    packet[2] |= 2; // TC: a TCP retry is needed, not an empty usable list.
    auto reply = parse_reply(packet, 0x1234);
    return reply.valid && reply.truncated && reply.servers.empty();
}());

static_assert(!parse_reply(response({}), 0x9999).valid);
static_assert(!parse_reply(std::vector<uint8_t>{0x12, 0x34}, 0x1234).valid);
static_assert([] {
    auto packet = response({{"de1.api.radio-browser.info", 443}});
    packet.pop_back();
    return !parse_reply(packet, 0x1234).valid;
}());
static_assert([] {
    auto packet = response({{"de1.api.radio-browser.info", 443}});
    const size_t answer = response({}).size();
    packet[answer + 1] = answer; // Self-referencing owner pointer.
    return !parse_reply(packet, 0x1234).valid;
}());
static_assert([] {
    auto packet = response({{"de1.api.radio-browser.info", 443}});
    const size_t answer = response({}).size();
    packet[answer + 10] = packet[answer + 11] = 255;
    return !parse_reply(packet, 0x1234).valid;
}());
static_assert([] {
    auto packet = response({});
    packet[3] = 0x83; // NXDOMAIN
    return !parse_reply(packet, 0x1234).valid;
}());
static_assert([] {
    auto reply = parse_reply(response({{"bad/host", 443}, {"-invalid.example", 443},
                                      {"valid.example", 0}, {"", 443}}), 0x1234);
    return reply.valid && reply.servers.empty();
}());
