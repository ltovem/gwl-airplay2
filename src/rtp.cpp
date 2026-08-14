#include "airplay2/rtp.h"

#include <array>
#include <chrono>
#include <cstring>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
using socket_length_t = int;
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
using socket_length_t = socklen_t;
#endif

namespace gwl::airplay2 {

class RtpReceiver::Impl {
public:
    int fd = -1;
    std::uint16_t port = 0;
};

RtpReceiver::RtpReceiver() : impl_(std::make_unique<Impl>()) {}

RtpReceiver::~RtpReceiver() { close(); }

bool RtpReceiver::bind(std::uint16_t requested_port) {
    if (running()) return true;
#if defined(_WIN32)
    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;
#endif

    impl_->fd = static_cast<int>(::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP));
    if (impl_->fd < 0) return false;

    int reuse = 1;
    setsockopt(impl_->fd, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&reuse), sizeof(reuse));

    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    local.sin_port = htons(requested_port);

    if (::bind(impl_->fd, reinterpret_cast<const sockaddr*>(&local), sizeof(local)) < 0) {
        close();
        return false;
    }

    sockaddr_in actual{};
    socket_length_t length = sizeof(actual);
    if (::getsockname(impl_->fd, reinterpret_cast<sockaddr*>(&actual), &length) == 0) {
        impl_->port = ntohs(actual.sin_port);
    }
    return true;
}

void RtpReceiver::close() {
    if (!impl_ || impl_->fd < 0) return;
#if defined(_WIN32)
    closesocket(impl_->fd);
    WSACleanup();
#else
    ::close(impl_->fd);
#endif
    impl_->fd = -1;
    impl_->port = 0;
}

bool RtpReceiver::running() const noexcept { return impl_ && impl_->fd >= 0; }

std::uint16_t RtpReceiver::port() const noexcept { return impl_ ? impl_->port : 0; }

bool RtpReceiver::receive(RtpPacket& packet, int timeout_ms) {
    if (!running()) return false;

    if (timeout_ms > 0) {
        fd_set read_set;
        FD_ZERO(&read_set);
        FD_SET(impl_->fd, &read_set);
        timeval timeout{};
        timeout.tv_sec = timeout_ms / 1000;
        timeout.tv_usec = (timeout_ms % 1000) * 1000;
        const int ready = ::select(impl_->fd + 1, &read_set, nullptr, nullptr, &timeout);
        if (ready <= 0) return false;
    }

    std::array<std::uint8_t, 65536> buffer{};
    sockaddr_in peer{};
    socket_length_t peer_length = sizeof(peer);
#if defined(_WIN32)
    const int received = ::recvfrom(impl_->fd, reinterpret_cast<char*>(buffer.data()),
                                    static_cast<int>(buffer.size()), 0,
                                    reinterpret_cast<sockaddr*>(&peer), &peer_length);
#else
    const int received = static_cast<int>(::recvfrom(impl_->fd, buffer.data(), buffer.size(), 0,
                                                      reinterpret_cast<sockaddr*>(&peer), &peer_length));
#endif
    if (received < 12) return false;

    const std::uint8_t version = static_cast<std::uint8_t>(buffer[0] >> 6);
    if (version != 2) return false;

    const bool extension = (buffer[0] & 0x10u) != 0;
    const std::uint8_t csrc_count = buffer[0] & 0x0fu;
    const bool marker = (buffer[1] & 0x80u) != 0;
    const std::size_t payload_type = buffer[1] & 0x7fu;
    std::size_t offset = 12 + static_cast<std::size_t>(csrc_count) * 4;
    if (offset > static_cast<std::size_t>(received)) return false;

    if (extension) {
        if (offset + 4 > static_cast<std::size_t>(received)) return false;
        const std::uint16_t extension_words =
            static_cast<std::uint16_t>((buffer[offset + 2] << 8) | buffer[offset + 3]);
        offset += 4 + static_cast<std::size_t>(extension_words) * 4;
        if (offset > static_cast<std::size_t>(received)) return false;
    }

    const std::size_t padding = buffer[0] & 0x20u ? buffer[received - 1] : 0;
    if (padding > static_cast<std::size_t>(received) - offset) return false;

    packet.payload_type = static_cast<std::uint8_t>(payload_type);
    packet.marker = marker;
    packet.sequence = static_cast<std::uint16_t>((buffer[2] << 8) | buffer[3]);
    packet.timestamp = (static_cast<std::uint32_t>(buffer[4]) << 24) |
                       (static_cast<std::uint32_t>(buffer[5]) << 16) |
                       (static_cast<std::uint32_t>(buffer[6]) << 8) |
                       static_cast<std::uint32_t>(buffer[7]);
    packet.ssrc = (static_cast<std::uint32_t>(buffer[8]) << 24) |
                  (static_cast<std::uint32_t>(buffer[9]) << 16) |
                  (static_cast<std::uint32_t>(buffer[10]) << 8) |
                  static_cast<std::uint32_t>(buffer[11]);
    packet.payload.assign(reinterpret_cast<const char*>(buffer.data() + offset),
                          reinterpret_cast<const char*>(buffer.data() + received - padding));
    return true;
}

} // namespace gwl::airplay2
