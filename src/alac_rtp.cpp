#include "airplay2/alac_rtp.h"

namespace gwl::airplay2 {

bool AlacRtpPayload::extract(const RtpPacket& packet, AlacRtpFrame& frame) {
    if (packet.payload.empty()) return false;

    frame.sequence = packet.sequence;
    frame.timestamp = packet.timestamp;
    frame.marker = packet.marker;
    frame.payload = packet.payload;
    return true;
}

} // namespace gwl::airplay2
