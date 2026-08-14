#pragma once

#include <map>
#include <string>
#include <vector>

namespace gwl::airplay2 {

struct SdpMedia {
    std::string type;
    std::string transport;
    int payload_type = -1;
    std::string codec;
    int clock_rate = 0;
    int channels = 0;
    std::map<std::string, std::string> attributes;
};

struct AirPlaySdp {
    std::string session_name;
    std::string rsaaeskey;
    std::string aesiv;
    std::string fmtp;
    std::string fingerprint;
    std::string group;
    std::vector<SdpMedia> media;

    bool has_audio() const noexcept;
    bool has_video() const noexcept;
};

bool parse_sdp(const std::string& text, AirPlaySdp& result);

} // namespace gwl::airplay2
