#pragma once

#include <cstdint>
#include <vector>

namespace gwl::airplay2 {

enum class MediaKind { Audio, Video };
enum class AudioCodec { Unknown, Alac, AacEld, AacLc, Pcm };
enum class VideoCodec { Unknown, H264, H265 };

struct MediaFrame {
    MediaKind kind = MediaKind::Audio;
    std::uint32_t timestamp = 0;
    std::uint16_t sequence = 0;
    bool marker = false;
    std::vector<std::uint8_t> payload;
};

struct AudioFormat {
    AudioCodec codec = AudioCodec::Unknown;
    std::uint32_t sample_rate = 0;
    std::uint16_t channels = 0;
    std::uint16_t bits_per_sample = 0;
};

struct VideoFormat {
    VideoCodec codec = VideoCodec::Unknown;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t clock_rate = 90000;
};

} // namespace gwl::airplay2
