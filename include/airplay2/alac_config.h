#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace gwl::airplay2 {

struct AlacConfig {
    std::uint32_t sample_rate = 0;
    std::uint16_t channels = 0;
    std::uint16_t bit_depth = 0;
    std::uint32_t frame_length = 0;
    std::uint8_t compatible_version = 0;
    std::uint8_t pb = 0;
    std::uint8_t mb = 0;
    std::uint8_t kb = 0;
    std::uint8_t num_channels = 0;
    std::uint16_t max_run = 0;
    std::uint32_t max_frame_bytes = 0;
    std::uint32_t avg_bit_rate = 0;
    std::uint32_t max_bit_rate = 0;
    std::vector<std::uint8_t> codec_data;

    bool valid() const noexcept;
};

// Parse an Apple Lossless Specific Config (ALAC config atom payload).
// The payload is expected to contain the 24-byte ALAC magic cookie body,
// without the surrounding atom header.
bool parse_alac_config(const std::vector<std::uint8_t>& data, AlacConfig& result);

// Best-effort extraction of common ALAC parameters from an SDP fmtp string.
// This intentionally does not guess missing codec fields.
bool parse_alac_fmtp(const std::string& fmtp, AlacConfig& result);

} // namespace gwl::airplay2
