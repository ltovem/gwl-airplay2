#pragma once

#include <memory>
#include <string>

#include "airplay2/audio_pipeline.h"

namespace gwl::airplay2 {

// Platform-specific audio backends are intentionally kept outside the protocol
// core. Applications can provide their own sink, or use this factory when a
// native backend is available for the target platform.
std::unique_ptr<AudioSink> create_platform_audio_sink();

// Returns a stable identifier useful for diagnostics and demo UI.
std::string platform_audio_backend_name();

} // namespace gwl::airplay2
