#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "airplay2/audio_pipeline.h"

namespace gwl::airplay2 {

enum class Platform {
    Unknown,
    MacOS,
    IOS,
    TvOS,
    Android,
    AndroidTV,
    Windows,
    Linux,
    FreeBSD,
};

struct PlatformInfo {
    Platform platform = Platform::Unknown;
    std::string name;
    std::string version;
};

PlatformInfo current_platform();

// Core library never owns UI/audio-framework objects. Applications provide
// native sinks (CoreAudio/AudioUnit, AAudio/Oboe, WASAPI, ALSA, etc.).
std::unique_ptr<AudioSink> create_default_audio_sink();

} // namespace gwl::airplay2
