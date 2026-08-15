#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace gwl::airplay2 {

struct RtpEndpoint {
    std::string address;
    std::uint16_t port = 0;
};

struct RtpPacket {
    std::uint8_t payload_type = 0;
    std::uint16_t sequence = 0;
    std::uint32_t timestamp = 0;
    std::uint32_t ssrc = 0;
    bool marker = false;
    std::vector<std::uint8_t> payload;
};

class RtpReceiver {
public:
    using PacketHandler = std::function<void(const RtpPacket&)>;

    RtpReceiver();
    ~RtpReceiver();

    RtpReceiver(const RtpReceiver&) = delete;
    RtpReceiver& operator=(const RtpReceiver&) = delete;

    bool bind(std::uint16_t port = 0);
    void close();
    bool running() const noexcept;
    std::uint16_t port() const noexcept;

    bool receive(RtpPacket& packet, int timeout_ms = 0);

    // Starts a background receive loop. The handler is invoked from the
    // receiver thread for each valid RTP packet.
    void set_packet_handler(PacketHandler handler);
    void clear_packet_handler();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace gwl::airplay2
