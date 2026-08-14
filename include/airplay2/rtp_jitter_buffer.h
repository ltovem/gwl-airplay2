#pragma once

#include "airplay2/rtp.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>

namespace gwl::airplay2 {

struct RtpStreamStats {
    std::uint64_t received = 0;
    std::uint64_t discarded = 0;
    std::uint64_t reordered = 0;
    std::uint64_t lost = 0;
};

class RtpJitterBuffer {
public:
    explicit RtpJitterBuffer(std::size_t capacity = 128);

    void reset() noexcept;
    bool push(RtpPacket packet);
    std::optional<RtpPacket> pop();

    const RtpStreamStats& stats() const noexcept { return stats_; }
    bool initialized() const noexcept { return initialized_; }
    std::uint16_t next_sequence() const noexcept { return expected_sequence_; }

private:
    static bool sequence_before(std::uint16_t a, std::uint16_t b) noexcept;

    std::size_t capacity_;
    bool initialized_ = false;
    std::uint16_t expected_sequence_ = 0;
    std::map<std::uint16_t, RtpPacket> packets_;
    RtpStreamStats stats_{};
};

} // namespace gwl::airplay2
