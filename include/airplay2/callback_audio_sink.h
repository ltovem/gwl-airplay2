#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

#include "airplay2/audio_pipeline.h"

namespace gwl::airplay2 {

// Platform-neutral PCM output bridge. UI/platform code can adapt this to
// CoreAudio, AVAudioEngine, AudioTrack/AAudio, WASAPI, ALSA/PipeWire, etc.
using PcmWriteCallback = std::function<bool(const std::int16_t* samples,
                                             std::size_t frames,
                                             const PcmFormat& format)>;

class CallbackAudioSink final : public AudioSink {
public:
    explicit CallbackAudioSink(PcmWriteCallback callback);

    bool open(const PcmFormat& format) override;
    void close() override;
    bool write(const std::int16_t* samples, std::size_t frames) override;

    const PcmFormat& format() const noexcept { return format_; }
    bool opened() const noexcept { return opened_; }

private:
    PcmWriteCallback callback_;
    PcmFormat format_{};
    bool opened_ = false;
};

} // namespace gwl::airplay2
