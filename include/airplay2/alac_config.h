#pragma once

#include <cstdint>
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

// Parse the 28-byte ALACSpecificConfig payload (magic cookie body).
bool parse_alac_config(const std::vector<std::uint8_t>& data, AlacConfig& result);

} // namespace gwl::airplay2
