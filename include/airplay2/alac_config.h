#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace gwl::airplay2 {

struct AlacConfig {
    std::uint32_t frame_length = 0;
    std::uint8_t compatible_version = 0;
    std::uint8_t pb = 0;
    std::uint8_t mb = 0;
    std::uint8_t kb = 0;
    std::uint8_t num_channels = 0;
    std::uint16_t max_run = 0;
    std::uint32_t max_frame_bytes = 0;
    std::uint32_t avg_bit_rate = 0;
    std::uint32_t sample_rate = 0;
    std::uint16_t bit_depth = 0;
    std::vector<std::uint8_t> codec_data;

    std::uint16_t channels() const noexcept { return num_channels; }
    bool valid() const noexcept;
};

// Parse the 24-byte ALAC magic-cookie payload.
bool parse_alac_config(const std::vector<std::uint8_t>& data, AlacConfig& result);

// Parse common AirPlay SDP fmtp parameters and merge them into the config.
bool parse_alac_fmtp(const std::string& fmtp, AlacConfig& result);

} // namespace gwl::airplay2
