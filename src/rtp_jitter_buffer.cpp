#include "airplay2/rtp_jitter_buffer.h"

namespace gwl::airplay2 {

RtpJitterBuffer::RtpJitterBuffer(std::size_t capacity)
    : capacity_(capacity ? capacity : 1) {}

void RtpJitterBuffer::reset() noexcept {
    initialized_ = false;
    expected_sequence_ = 0;
    packets_.clear();
    stats_ = {};
}

bool RtpJitterBuffer::sequence_before(std::uint16_t a, std::uint16_t b) noexcept {
    return static_cast<std::int16_t>(a - b) < 0;
}

bool RtpJitterBuffer::push(RtpPacket packet) {
    ++stats_.received;

    if (!initialized_) {
        expected_sequence_ = packet.sequence;
        initialized_ = true;
    }

    if (sequence_before(packet.sequence, expected_sequence_)) {
        ++stats_.discarded;
        return false;
    }

    if (packets_.find(packet.sequence) != packets_.end()) {
        ++stats_.discarded;
        return false;
    }

    if (packet.sequence != expected_sequence_) ++stats_.reordered;
    packets_.emplace(packet.sequence, std::move(packet));

    while (packets_.size() > capacity_) {
        auto it = packets_.begin();
        if (it->first == expected_sequence_) {
            ++expected_sequence_;
        } else {
            ++stats_.lost;
            expected_sequence_ = static_cast<std::uint16_t>(it->first + 1);
        }
        packets_.erase(it);
    }
    return true;
}

std::optional<RtpPacket> RtpJitterBuffer::pop() {
    if (!initialized_) return std::nullopt;
    auto it = packets_.find(expected_sequence_);
    if (it == packets_.end()) return std::nullopt;

    RtpPacket packet = std::move(it->second);
    packets_.erase(it);
    expected_sequence_ = static_cast<std::uint16_t>(expected_sequence_ + 1);
    return packet;
}

} // namespace gwl::airplay2
