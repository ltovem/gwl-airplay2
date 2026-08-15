#include "airplay2/mdns.h"

#include <cstdint>
#include <cstring>
#include <vector>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace gwl::airplay2 {
namespace {
constexpr std::uint16_t kMdnsPort = 5353;
constexpr std::uint32_t kMdnsAddress = 0xE00000FBu;

void u16(std::vector<std::uint8_t>& b, std::uint16_t v) {
    b.push_back(static_cast<std::uint8_t>(v >> 8));
    b.push_back(static_cast<std::uint8_t>(v));
}
void u32(std::vector<std::uint8_t>& b, std::uint32_t v) {
    b.push_back(static_cast<std::uint8_t>(v >> 24));
    b.push_back(static_cast<std::uint8_t>(v >> 16));
    b.push_back(static_cast<std::uint8_t>(v >> 8));
    b.push_back(static_cast<std::uint8_t>(v));
}
void name(std::vector<std::uint8_t>& b, const std::string& value) {
    std::size_t start = 0;
    while (start < value.size()) {
        const auto dot = value.find('.', start);
        const auto end = dot == std::string::npos ? value.size() : dot;
        const auto len = end - start;
        if (len > 63) return;
        b.push_back(static_cast<std::uint8_t>(len));
        b.insert(b.end(), value.begin() + static_cast<std::ptrdiff_t>(start),
                 value.begin() + static_cast<std::ptrdiff_t>(end));
        start = dot == std::string::npos ? value.size() : dot + 1;
    }
    b.push_back(0);
}
void header(std::vector<std::uint8_t>& b) {
    u16(b, 0); u16(b, 0x8400); u16(b, 0); u16(b, 1); u16(b, 0); u16(b, 0);
}
}

class MdnsService::Impl {
public:
    int socket_fd = -1;
    bool active = false;
};

MdnsService::MdnsService() : impl_(new Impl) {}
MdnsService::~MdnsService() { unpublish(); delete impl_; }

bool MdnsService::publish(const std::string& instance_name, std::uint16_t port,
                          const std::vector<MdnsTxtRecord>& records) {
    if (impl_->active) return true;
#if defined(_WIN32)
    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;
#endif
    impl_->socket_fd = static_cast<int>(::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP));
    if (impl_->socket_fd < 0) return false;

    const int reuse = 1;
    if (setsockopt(impl_->socket_fd, SOL_SOCKET, SO_REUSEADDR,
                   reinterpret_cast<const char*>(&reuse), sizeof(reuse)) != 0) {
        unpublish();
        return false;
    }

#if defined(_WIN32)
    // Winsock SDKs do not expose IP_MULTICAST_TTL consistently across all
    // supported MSVC/Windows SDK combinations. The Winsock option value is
    // stable, so keep the portability detail local to this implementation.
    constexpr int kIpMulticastTtl = 10;
    const DWORD ttl = 255;
    if (setsockopt(impl_->socket_fd, IPPROTO_IP, kIpMulticastTtl,
                   reinterpret_cast<const char*>(&ttl), sizeof(ttl)) != 0) {
        unpublish();
        return false;
    }
#else
    const unsigned char ttl = 255;
    if (setsockopt(impl_->socket_fd, IPPROTO_IP, IP_MULTICAST_TTL,
                   &ttl, sizeof(ttl)) != 0) {
        unpublish();
        return false;
    }
#endif

    sockaddr_in dst{};
    dst.sin_family = AF_INET;
    dst.sin_port = htons(kMdnsPort);
    dst.sin_addr.s_addr = htonl(kMdnsAddress);

    const std::string service = instance_name + "._airplay._tcp.local";
    std::vector<std::uint8_t> packet;
    header(packet);
    name(packet, service);
    u16(packet, 33); u16(packet, 0x8001); u32(packet, 120);
    std::vector<std::uint8_t> srv;
    u16(srv, 0); u16(srv, 0); u16(srv, port); name(srv, "gwl-airplay.local");
    u16(packet, static_cast<std::uint16_t>(srv.size()));
    packet.insert(packet.end(), srv.begin(), srv.end());

#if defined(_WIN32)
    const int sent = sendto(impl_->socket_fd, reinterpret_cast<const char*>(packet.data()),
                            static_cast<int>(packet.size()), 0,
                            reinterpret_cast<const sockaddr*>(&dst), sizeof(dst));
#else
    const int sent = static_cast<int>(sendto(impl_->socket_fd, packet.data(), packet.size(), 0,
                                              reinterpret_cast<const sockaddr*>(&dst), sizeof(dst)));
#endif
    if (sent < 0) { unpublish(); return false; }

    packet.clear(); header(packet); name(packet, service);
    u16(packet, 16); u16(packet, 0x8001); u32(packet, 4500);
    std::vector<std::uint8_t> txt;
    for (const auto& r : records) {
        const std::string item = r.key + "=" + r.value;
        if (item.size() <= 255) {
            txt.push_back(static_cast<std::uint8_t>(item.size()));
            txt.insert(txt.end(), item.begin(), item.end());
        }
    }
    u16(packet, static_cast<std::uint16_t>(txt.size()));
    packet.insert(packet.end(), txt.begin(), txt.end());
#if defined(_WIN32)
    sendto(impl_->socket_fd, reinterpret_cast<const char*>(packet.data()), static_cast<int>(packet.size()),
           0, reinterpret_cast<const sockaddr*>(&dst), sizeof(dst));
#else
    sendto(impl_->socket_fd, packet.data(), packet.size(), 0,
           reinterpret_cast<const sockaddr*>(&dst), sizeof(dst));
#endif
    impl_->active = true;
    return true;
}

void MdnsService::unpublish() {
    if (!impl_ || impl_->socket_fd < 0) return;
#if defined(_WIN32)
    closesocket(static_cast<SOCKET>(impl_->socket_fd));
    WSACleanup();
#else
    close(impl_->socket_fd);
#endif
    impl_->socket_fd = -1; impl_->active = false;
}

bool MdnsService::published() const noexcept { return impl_ && impl_->active; }

} // namespace gwl::airplay2
