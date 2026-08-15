#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "airplay2/audio_pipeline.h"

namespace gwl::airplay2 {

struct ReceiverConfig {
    std::string device_name = "GWL AirPlay";
    std::string device_id;
    std::uint16_t port = 7000;
    bool enable_audio = true;
    bool enable_video = true;

    // Optional audio sink factory. When supplied, each RTSP session gets a
    // fresh sink and can build its own ALAC pipeline from ANNOUNCE/SDP.
    std::function<std::unique_ptr<AudioSink>()> audio_sink_factory;
};

class AirPlayReceiver {
public:
    AirPlayReceiver();
    ~AirPlayReceiver();

    AirPlayReceiver(const AirPlayReceiver&) = delete;
    AirPlayReceiver& operator=(const AirPlayReceiver&) = delete;

    bool start(const ReceiverConfig& config);
    void stop();
    bool running() const noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace gwl::airplay2
