#pragma once

#include "airplay2/audio_pipeline.h"

#include <memory>

namespace gwl::airplay2 {

// Native Apple audio output for macOS, iOS, and tvOS.
// The protocol core remains platform-neutral; this class is an optional
// platform adapter built only for Apple targets.
class AppleAudioSink final : public AudioSink {
public:
    AppleAudioSink();
    ~AppleAudioSink() override;

    AppleAudioSink(const AppleAudioSink&) = delete;
    AppleAudioSink& operator=(const AppleAudioSink&) = delete;

    bool open(const PcmFormat& format) override;
    void close() override;
    bool write(const std::int16_t* samples, std::size_t frames) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace gwl::airplay2
