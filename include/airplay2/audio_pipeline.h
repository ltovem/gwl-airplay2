#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "airplay2/alac_decoder.h"

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

    // samples points to interleaved PCM. frames is the number of PCM frames,
    // not the number of scalar samples in the interleaved buffer.
    virtual bool write(const std::int16_t* samples, std::size_t frames) = 0;
};

// Bridges the RTP/codec layer to a platform AudioSink. The decoder is the
// same portable AlacDecoder implementation on every platform; only the sink
// is platform-specific.
class AudioPipeline {
public:
    AudioPipeline(std::unique_ptr<AlacDecoder> decoder,
                  std::unique_ptr<AudioSink> sink);
    ~AudioPipeline();

    AudioPipeline(const AudioPipeline&) = delete;
    AudioPipeline& operator=(const AudioPipeline&) = delete;

    bool configure(const AlacConfig& config);
    bool push(const std::vector<std::uint8_t>& packet);
    void reset();

    bool opened() const noexcept { return opened_; }
    const PcmFormat& format() const noexcept { return format_; }

private:
    std::unique_ptr<AlacDecoder> decoder_;
    std::unique_ptr<AudioSink> sink_;
    PcmFormat format_{};
    bool opened_ = false;
};

} // namespace gwl::airplay2
