#include "airplay2/sdp.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace gwl::airplay2 {
namespace {

std::string trim(std::string value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) value.erase(value.begin());
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) value.pop_back();
    return value;
}

void parse_attribute(const std::string& value, SdpMedia* media, AirPlaySdp& result) {
    const auto colon = value.find(':');
    const auto equal = value.find('=');
    const auto split = std::min(colon == std::string::npos ? value.size() : colon,
                                equal == std::string::npos ? value.size() : equal);
    const auto key = value.substr(0, split);
    const auto data = split < value.size() ? value.substr(split + 1) : std::string{};

    if (media) media->attributes[key] = data;
    if (key == "rsaaeskey") result.rsaaeskey = data;
    else if (key == "aesiv") result.aesiv = data;
    else if (key == "fmtp") result.fmtp = data;
    else if (key == "fingerprint") result.fingerprint = data;
    else if (key == "group") result.group = data;

    if (media && key == "rtpmap") {
        std::istringstream stream(data);
        std::string payload;
        std::getline(stream, payload, ' ');
        if (payload.find('/') != std::string::npos) {
            media->codec = payload;
            const auto slash1 = payload.find('/');
            const auto slash2 = payload.find('/', slash1 + 1);
            try {
                media->clock_rate = std::stoi(payload.substr(slash1 + 1,
                    slash2 == std::string::npos ? std::string::npos : slash2 - slash1 - 1));
                if (slash2 != std::string::npos) media->channels = std::stoi(payload.substr(slash2 + 1));
            } catch (...) {
                media->clock_rate = 0;
                media->channels = 0;
            }
        }
    }
}

} // namespace

bool AirPlaySdp::has_audio() const noexcept {
    return std::any_of(media.begin(), media.end(), [](const auto& item) { return item.type == "audio"; });
}

bool AirPlaySdp::has_video() const noexcept {
    return std::any_of(media.begin(), media.end(), [](const auto& item) { return item.type == "video"; });
}

bool parse_sdp(const std::string& text, AirPlaySdp& result) {
    result = {};
    std::istringstream input(text);
    std::string line;
    SdpMedia* current = nullptr;
    bool has_version = false;

    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        line = trim(line);
        if (line.empty()) continue;

        if (line.rfind("v=", 0) == 0) {
            has_version = line == "v=0";
        } else if (line.rfind("s=", 0) == 0) {
            result.session_name = line.substr(2);
        } else if (line.rfind("m=", 0) == 0) {
            std::istringstream media_line(line.substr(2));
            SdpMedia media;
            std::string port;
            std::string payload;
            media_line >> media.type >> port >> media.transport >> payload;
            if (!payload.empty()) {
                try { media.payload_type = std::stoi(payload); } catch (...) { media.payload_type = -1; }
            }
            result.media.push_back(std::move(media));
            current = &result.media.back();
        } else if (line.rfind("a=", 0) == 0) {
            parse_attribute(line.substr(2), current, result);
        }
    }

    return has_version && !result.media.empty();
}

} // namespace gwl::airplay2
