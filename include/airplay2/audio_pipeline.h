#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "airplay2/media_frame.h"

namespace gwl::airplay2 {

struct PcmFormat {
    std::uint32_t sample_rate = 44100;
    std::uint16_t channels = 2;
    std::uint16_t bits_per_sample = 16;
};

class AudioSink {
public:
    virtual ~AudioSink() = default;
    virtual bool open(const PcmFormat& format) = 0;
    virtual void close() = 0;
    virtual bool write(const std::int16_t* samples, std::size_t frames) = 0;
};

class AudioDecoder {
public:
    virtual ~AudioDecoder() = default;
    virtual bool configure(const std::vector<std::uint8_t>& codec_data) = 0;
    virtual bool decode(const std::uint8_t* data, std::size_t size,
                        std::vector<std::int16_t>& pcm) = 0;
};

class AudioPipeline {
public:
    AudioPipeline(std::unique_ptr<AudioDecoder> decoder,
                  std::unique_ptr<AudioSink> sink);
    ~AudioPipeline();

    AudioPipeline(const AudioPipeline&) = delete;
    AudioPipeline& operator=(const AudioPipeline&) = delete;

    bool configure(const PcmFormat& format, const std::vector<std::uint8_t>& codec_data);
    bool push(const std::uint8_t* data, std::size_t size);
    void reset();

private:
    std::unique_ptr<AudioDecoder> decoder_;
    std::unique_ptr<AudioSink> sink_;
    bool opened_ = false;
};

} // namespace gwl::airplay2
