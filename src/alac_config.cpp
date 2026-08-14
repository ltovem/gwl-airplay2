#include "airplay2/alac_config.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>

namespace gwl::airplay2 {
namespace {

std::uint32_t be32(const std::uint8_t* p) {
    return (static_cast<std::uint32_t>(p[0]) << 24) |
           (static_cast<std::uint32_t>(p[1]) << 16) |
           (static_cast<std::uint32_t>(p[2]) << 8) |
           static_cast<std::uint32_t>(p[3]);
}

std::uint16_t be16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(p[0]) << 8) | p[1]);
}

std::string trim(std::string value) {
    auto not_space = [](unsigned char c) { return !std::isspace(c); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
    return value;
}

bool parse_uint(const std::string& value, std::uint32_t& out) {
    try {
        const auto cleaned = trim(value);
        std::size_t consumed = 0;
        const auto n = std::stoul(cleaned, &consumed, 0);
        if (consumed != cleaned.size() || n > 0xffffffffUL) return false;
        out = static_cast<std::uint32_t>(n);
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace

bool AlacConfig::valid() const noexcept {
    return frame_length != 0 && num_channels != 0 && sample_rate != 0 &&
           bit_depth != 0;
}

bool parse_alac_config(const std::vector<std::uint8_t>& data, AlacConfig& result) {
    if (data.size() < 24) return false;

    AlacConfig cfg;
    cfg.frame_length = be32(data.data());
    cfg.compatible_version = data[4];
    cfg.pb = data[5];
    cfg.mb = data[6];
    cfg.kb = data[7];
    cfg.num_channels = data[8];
    cfg.max_run = be16(data.data() + 9);
    cfg.max_frame_bytes = be32(data.data() + 11);
    cfg.avg_bit_rate = be32(data.data() + 15);
    cfg.sample_rate = be32(data.data() + 19);
    cfg.codec_data.assign(data.begin(), data.begin() + 24);
    result = std::move(cfg);
    return true;
}

bool parse_alac_fmtp(const std::string& fmtp, AlacConfig& result) {
    AlacConfig cfg = result;
    std::string token;
    std::istringstream stream(fmtp);
    while (std::getline(stream, token, ';')) {
        const auto eq = token.find('=');
        if (eq == std::string::npos) continue;
        const auto key = trim(token.substr(0, eq));
        const auto value = trim(token.substr(eq + 1));
        std::uint32_t number = 0;
        if (!parse_uint(value, number)) continue;

        if (key == "sampleRate" || key == "sample_rate" || key == "sample-rate") {
            cfg.sample_rate = number;
        } else if (key == "channels") {
            if (number > 255) return false;
            cfg.num_channels = static_cast<std::uint8_t>(number);
        } else if (key == "bitDepth" || key == "bit_depth" || key == "bit-depth") {
            if (number > 65535) return false;
            cfg.bit_depth = static_cast<std::uint16_t>(number);
        } else if (key == "frameLength" || key == "frame_length" || key == "frame-length") {
            cfg.frame_length = number;
        }
    }

    if (!cfg.valid()) return false;
    result = std::move(cfg);
    return true;
}

} // namespace gwl::airplay2
