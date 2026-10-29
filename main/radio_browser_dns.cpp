#include "radio_browser_dns.hpp"
#include "radio.hpp"
#include <cerrno>
#include <cstring>
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "lwip/sockets.h"
#include <fcntl.h>
#include <unistd.h>

namespace p4radio {
namespace {
constexpr size_t kMaxReply = 4096;
struct Socket {
    int fd;
    ~Socket() { if (fd >= 0) close(fd); }
};
struct Deadline {
    int station = g_station.load();
    uint32_t token = g_station_token.load();
    bool playback = true;
    int64_t until = esp_timer_get_time() + 2500000;
    bool active() const {
        return esp_timer_get_time() < until && g_connected.load() && (!playback ||
            (g_playback_requested.load() && !g_sleep_requested.load() && !g_scan_requested.load() &&
             g_station.load() == station && g_station_token.load() == token));
    }
};

bool wait_socket(int fd, bool writing, const Deadline &deadline)
{
    while (deadline.active()) {
        fd_set ready;
        FD_ZERO(&ready);
        FD_SET(fd, &ready);
        timeval timeout{0, 100000};
        const int result = select(fd + 1, writing ? nullptr : &ready, writing ? &ready : nullptr, nullptr, &timeout);
        if (result > 0) return true;
        if (result < 0 && errno != EINTR) return false;
    }
    return false;
}

bool transfer(int fd, uint8_t *data, size_t size, bool writing, const Deadline &deadline)
{
    size_t offset = 0;
    while (offset < size && wait_socket(fd, writing, deadline)) {
        const int count = writing ? send(fd, data + offset, size - offset, 0) : recv(fd, data + offset, size - offset, 0);
        if (count > 0) offset += count;
        else if (count == 0 || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) return false;
    }
    return offset == size;
}

std::vector<uint8_t> query(const sockaddr_storage &resolver, socklen_t length,
                           const std::vector<uint8_t> &question, bool tcp, int station, uint32_t token, bool playback)
{
    Socket socket{::socket(resolver.ss_family, tcp ? SOCK_STREAM : SOCK_DGRAM, tcp ? IPPROTO_TCP : IPPROTO_UDP)};
    if (socket.fd < 0 || fcntl(socket.fd, F_SETFL, O_NONBLOCK) < 0) return {};
    Deadline deadline;
    deadline.station = station;
    deadline.token = token;
    deadline.playback = playback;
    if (connect(socket.fd, reinterpret_cast<const sockaddr *>(&resolver), length) < 0) {
        if (errno != EINPROGRESS || !wait_socket(socket.fd, true, deadline)) return {};
        int error = 0;
        socklen_t error_size = sizeof(error);
        if (getsockopt(socket.fd, SOL_SOCKET, SO_ERROR, &error, &error_size) < 0 || error) return {};
    }
    std::vector<uint8_t> request;
    if (tcp) {
        request.push_back(static_cast<uint8_t>(question.size() >> 8));
        request.push_back(static_cast<uint8_t>(question.size()));
    }
    request.insert(request.end(), question.begin(), question.end());
    if (!transfer(socket.fd, request.data(), request.size(), true, deadline)) return {};
    size_t reply_size = kMaxReply;
    if (tcp) {
        uint8_t prefix[2];
        if (!transfer(socket.fd, prefix, sizeof(prefix), false, deadline)) return {};
        reply_size = (static_cast<size_t>(prefix[0]) << 8) | prefix[1];
        if (reply_size < 12 || reply_size > kMaxReply) return {};
    }
    // Keep DNS buffers off the playback task's 8 KB stack.
    std::vector<uint8_t> reply(reply_size);
    if (tcp) {
        if (!transfer(socket.fd, reply.data(), reply.size(), false, deadline)) return {};
    } else {
        if (!wait_socket(socket.fd, false, deadline)) return {};
        const int count = recv(socket.fd, reply.data(), reply.size(), 0);
        if (count <= 0) return {};
        reply.resize(count);
    }
    return reply;
}

std::vector<uint8_t> question(uint16_t id)
{
    std::vector<uint8_t> packet{static_cast<uint8_t>(id >> 8), static_cast<uint8_t>(id),
                              1, 0, 0, 1, 0, 0, 0, 0, 0, 0};
    const std::string name = radio_browser_dns::kService;
    size_t start = 0;
    while (start < name.size()) {
        const size_t dot = name.find('.', start);
        const size_t end = dot == std::string::npos ? name.size() : dot;
        packet.push_back(static_cast<uint8_t>(end - start));
        packet.insert(packet.end(), name.begin() + start, name.begin() + end);
        start = end + 1;
    }
    // SRV, IN. A truncated UDP answer is retried over TCP.
    packet.insert(packet.end(), {0, 0, 33, 0, 1});
    return packet;
}
} // namespace

std::vector<std::string> discover_radio_browser_servers(bool for_playback)
{
    std::vector<std::string> servers;
    esp_netif_t *netif = esp_netif_get_default_netif();
    if (!netif || !g_connected.load()) return servers;
    const uint16_t id = static_cast<uint16_t>(esp_random());
    const int station = g_station.load();
    const uint32_t token = g_station_token.load();
    const auto packet = question(id);
    std::vector<sockaddr_storage> tried;
    for (auto kind : {ESP_NETIF_DNS_MAIN, ESP_NETIF_DNS_BACKUP, ESP_NETIF_DNS_FALLBACK}) {
        Deadline cancellation;
        cancellation.station = station;
        cancellation.token = token;
        cancellation.playback = for_playback;
        if (!cancellation.active()) break;
        esp_netif_dns_info_t info{};
        if (esp_netif_get_dns_info(netif, kind, &info) != ESP_OK) continue;
        sockaddr_storage address{};
        socklen_t size;
        if (info.ip.type == ESP_IPADDR_TYPE_V4 && info.ip.u_addr.ip4.addr) {
            auto *v4 = reinterpret_cast<sockaddr_in *>(&address);
            v4->sin_family = AF_INET;
            v4->sin_port = htons(53);
            v4->sin_addr.s_addr = info.ip.u_addr.ip4.addr;
            size = sizeof(*v4);
        }
#if CONFIG_LWIP_IPV6
        else if (info.ip.type == ESP_IPADDR_TYPE_V6) {
            auto *v6 = reinterpret_cast<sockaddr_in6 *>(&address);
            v6->sin6_family = AF_INET6;
            v6->sin6_port = htons(53);
            std::memcpy(&v6->sin6_addr, info.ip.u_addr.ip6.addr, sizeof(v6->sin6_addr));
            if (IN6_IS_ADDR_UNSPECIFIED(&v6->sin6_addr)) continue;
            if (IN6_IS_ADDR_LINKLOCAL(&v6->sin6_addr))
                v6->sin6_scope_id = esp_netif_get_netif_impl_index(netif);
            size = sizeof(*v6);
        }
#endif
        else continue;
        if (std::any_of(tried.begin(), tried.end(), [&address](const auto &previous) {
            return std::memcmp(&address, &previous, sizeof(address)) == 0;
        })) continue;
        tried.push_back(address);
        auto reply = radio_browser_dns::parse_reply(query(address, size, packet, false, station, token, for_playback), id);
        if (reply.valid && reply.truncated)
            reply = radio_browser_dns::parse_reply(query(address, size, packet, true, station, token, for_playback), id);
        if (!reply.valid || reply.truncated) continue;
        for (const auto &server : reply.servers)
            servers.push_back("https://" + server.host + (server.port == 443 ? "" : ":" + std::to_string(server.port)));
        if (!servers.empty()) break;
    }
    // Radio Browser recommends random order and retrying another server on failure.
    for (size_t i = servers.size(); i > 1; --i) std::swap(servers[i - 1], servers[esp_random() % i]);
    if (servers.empty()) ESP_LOGW(kTag, "Radio Browser SRV discovery failed; using station fallbacks");
    else ESP_LOGI(kTag, "Radio Browser: discovered %u servers using %s", static_cast<unsigned>(servers.size()), radio_browser_dns::kService);
    return servers;
}
} // namespace p4radio
