#include "airplay2/mdns.h"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <string>
#include <thread>
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
constexpr std::uint32_t kTtl = 120;

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
void header(std::vector<std::uint8_t>& b, std::uint16_t flags,
            std::uint16_t qd, std::uint16_t an) {
    u16(b, 0); u16(b, flags); u16(b, qd); u16(b, an); u16(b, 0); u16(b, 0);
}

#if defined(_WIN32)
using Socket = SOCKET;
constexpr Socket kInvalidSocket = INVALID_SOCKET;
#else
using Socket = int;
constexpr Socket kInvalidSocket = -1;
#endif

void close_socket(Socket s) {
#if defined(_WIN32)
    if (s != kInvalidSocket) closesocket(s);
#else
    if (s != kInvalidSocket) close(s);
#endif
}

} // namespace

class MdnsService::Impl {
public:
    Socket socket_fd = kInvalidSocket;
    std::atomic<bool> active{false};
    std::thread responder;
    std::string instance;
    std::string target = "gwl-airplay.local";
    std::uint16_t port = 0;
    std::vector<MdnsTxtRecord> records;
    std::uint32_t local_address = 0;

    std::vector<std::uint8_t> make_response() const {
        const std::string service = instance + "._airplay._tcp.local";
        std::vector<std::uint8_t> packet;
        // Unsolicited/response packet: PTR + SRV + TXT + A.
        header(packet, 0x8400, 0, 4);

        name(packet, "_airplay._tcp.local");
        u16(packet, 12); u16(packet, 0x8001); u32(packet, kTtl);
        std::vector<std::uint8_t> ptr; name(ptr, service);
        u16(packet, static_cast<std::uint16_t>(ptr.size()));
        packet.insert(packet.end(), ptr.begin(), ptr.end());

        name(packet, service);
        u16(packet, 33); u16(packet, 0x8001); u32(packet, kTtl);
        std::vector<std::uint8_t> srv;
        u16(srv, 0); u16(srv, 0); u16(srv, port); name(srv, target);
        u16(packet, static_cast<std::uint16_t>(srv.size()));
        packet.insert(packet.end(), srv.begin(), srv.end());

        name(packet, service);
        u16(packet, 16); u16(packet, 0x8001); u32(packet, kTtl);
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

        name(packet, target);
        u16(packet, 1); u16(packet, 0x8001); u32(packet, kTtl); u16(packet, 4);
        u32(packet, local_address);
        return packet;
    }

    void send_response(const sockaddr_in* destination = nullptr) {
        const auto packet = make_response();
        sockaddr_in dst{};
        if (destination) {
            dst = *destination;
        } else {
            dst.sin_family = AF_INET;
            dst.sin_port = htons(kMdnsPort);
            dst.sin_addr.s_addr = htonl(kMdnsAddress);
        }
#if defined(_WIN32)
        sendto(socket_fd, reinterpret_cast<const char*>(packet.data()),
               static_cast<int>(packet.size()), 0,
               reinterpret_cast<const sockaddr*>(&dst), sizeof(dst));
#else
        sendto(socket_fd, packet.data(), packet.size(), 0,
               reinterpret_cast<const sockaddr*>(&dst), sizeof(dst));
#endif
    }

    void run() {
        while (active.load()) {
            std::uint8_t buffer[1500]{};
            sockaddr_in from{};
#if defined(_WIN32)
            int from_len = sizeof(from);
            const int n = recvfrom(socket_fd, reinterpret_cast<char*>(buffer), sizeof(buffer), 0,
                                   reinterpret_cast<sockaddr*>(&from), &from_len);
#else
            socklen_t from_len = sizeof(from);
            const int n = static_cast<int>(recvfrom(socket_fd, buffer, sizeof(buffer), 0,
                                                     reinterpret_cast<sockaddr*>(&from), &from_len));
#endif
            if (!active.load()) break;
            if (n < 12) continue;

            // We only need to know that this is a DNS query for the AirPlay
            // service. A full DNS name decompressor is unnecessary because
            // the responder always answers with its complete authoritative set.
            bool query_for_airplay = false;
            for (int i = 12; i + 18 < n; ++i) {
                if (std::memcmp(buffer + i, "_airplay", 8) == 0) {
                    query_for_airplay = true;
                    break;
                }
            }
            if (query_for_airplay) send_response(&from);
        }
    }
};

MdnsService::MdnsService() : impl_(new Impl) {}
MdnsService::~MdnsService() { unpublish(); delete impl_; }

bool MdnsService::publish(const std::string& instance_name, std::uint16_t service_port,
                          const std::vector<MdnsTxtRecord>& txt_records) {
    if (impl_->active.load()) return true;
#if defined(_WIN32)
    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;
#endif

    impl_->socket_fd = static_cast<Socket>(::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP));
    if (impl_->socket_fd == kInvalidSocket) {
#if defined(_WIN32)
        WSACleanup();
#endif
        return false;
    }

    const int reuse = 1;
    if (setsockopt(impl_->socket_fd, SOL_SOCKET, SO_REUSEADDR,
                   reinterpret_cast<const char*>(&reuse), sizeof(reuse)) != 0) {
        unpublish();
        return false;
    }

    sockaddr_in bind_addr{};
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_port = htons(kMdnsPort);
    bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (::bind(impl_->socket_fd, reinterpret_cast<const sockaddr*>(&bind_addr), sizeof(bind_addr)) != 0) {
        unpublish();
        return false;
    }

    ip_mreq membership{};
    membership.imr_multiaddr.s_addr = htonl(kMdnsAddress);
    membership.imr_interface.s_addr = htonl(INADDR_ANY);
    if (setsockopt(impl_->socket_fd, IPPROTO_IP, IP_ADD_MEMBERSHIP,
                   reinterpret_cast<const char*>(&membership), sizeof(membership)) != 0) {
        unpublish();
        return false;
    }

    // Discover the IPv4 address used for the normal LAN interface without
    // sending any packets. This address is placed in the A record for the
    // SRV target so senders can connect to the advertised RTSP port.
    {
        const Socket probe = static_cast<Socket>(::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP));
        if (probe != kInvalidSocket) {
            sockaddr_in remote{};
            remote.sin_family = AF_INET;
            remote.sin_port = htons(9);
            inet_pton(AF_INET, "192.0.2.1", &remote.sin_addr);
            if (::connect(probe, reinterpret_cast<const sockaddr*>(&remote), sizeof(remote)) == 0) {
                sockaddr_in local{};
#if defined(_WIN32)
                int len = sizeof(local);
                if (getsockname(probe, reinterpret_cast<sockaddr*>(&local), &len) == 0)
                    impl_->local_address = local.sin_addr.s_addr;
#else
                socklen_t len = sizeof(local);
                if (getsockname(probe, reinterpret_cast<sockaddr*>(&local), &len) == 0)
                    impl_->local_address = local.sin_addr.s_addr;
#endif
            }
            close_socket(probe);
        }
    }
    if (impl_->local_address == 0) {
        impl_->local_address = htonl(INADDR_LOOPBACK);
    }

#if defined(_WIN32)
    const DWORD ttl = 255;
    constexpr int kIpMulticastTtl = 10;
    setsockopt(impl_->socket_fd, IPPROTO_IP, kIpMulticastTtl,
               reinterpret_cast<const char*>(&ttl), sizeof(ttl));
#else
    const unsigned char ttl = 255;
    setsockopt(impl_->socket_fd, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));
#endif

    impl_->instance = instance_name;
    impl_->port = service_port;
    impl_->records = txt_records;
    impl_->active.store(true);
    impl_->responder = std::thread([this] { impl_->run(); });

    // Send unsolicited announcement as soon as we start, then let the query
    // responder handle subsequent iPhone/iPad discovery queries.
    impl_->send_response();
    return true;
}

void MdnsService::unpublish() {
    if (!impl_) return;
    const bool was_active = impl_->active.exchange(false);
    if (impl_->socket_fd != kInvalidSocket) {
#if defined(_WIN32)
        closesocket(impl_->socket_fd);
#else
        shutdown(impl_->socket_fd, SHUT_RDWR);
        close(impl_->socket_fd);
#endif
        impl_->socket_fd = kInvalidSocket;
    }
    if (impl_->responder.joinable()) impl_->responder.join();
#if defined(_WIN32)
    if (was_active) WSACleanup();
#else
    (void)was_active;
#endif
}

bool MdnsService::published() const noexcept { return impl_ && impl_->active.load(); }

} // namespace gwl::airplay2
