#pragma once

#include <cstdint>
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
    RtpReceiver();
    ~RtpReceiver();

    RtpReceiver(const RtpReceiver&) = delete;
    RtpReceiver& operator=(const RtpReceiver&) = delete;

    bool bind(std::uint16_t port = 0);
    void close();
    bool running() const noexcept;
    std::uint16_t port() const noexcept;

    bool receive(RtpPacket& packet, int timeout_ms = 0);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace gwl::airplay2
