#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "airplay2/rtp.h"

namespace gwl::airplay2 {

struct AlacRtpFrame {
    std::uint16_t sequence = 0;
    std::uint32_t timestamp = 0;
    bool marker = false;
    std::vector<std::uint8_t> payload;
};

class AlacRtpPayload {
public:
    // Extract the codec payload from an RTP packet. This intentionally does not
    // interpret or decrypt protected media; it only normalizes RTP framing.
    static bool extract(const RtpPacket& packet, AlacRtpFrame& frame);
};

} // namespace gwl::airplay2
