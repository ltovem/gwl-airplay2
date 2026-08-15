#include "airplay2/airplay_receiver.h"

#if defined(__APPLE__)
#include "airplay2/apple_audio_sink.h"
#endif

#include <chrono>
#include <iostream>
#include <thread>

int main() {
    gwl::airplay2::ReceiverConfig config;
    config.device_name = "GWL AirPlay";
    config.port = 7000;

#if defined(__APPLE__)
    // The protocol core creates one software ALAC decoder per RTSP session.
    // AppleAudioSink is only the platform output adapter; no Apple framework
    // types leak into the cross-platform core API.
    config.audio_sink_factory = []() -> std::unique_ptr<gwl::airplay2::AudioSink> {
        return std::make_unique<gwl::airplay2::AppleAudioSink>();
    };
#endif

    gwl::airplay2::AirPlayReceiver receiver;
    if (!receiver.start(config)) {
        std::cerr << "Failed to start GWL AirPlay receiver\n";
        return 1;
    }

    std::cout << "GWL AirPlay receiver started on port " << config.port << "\n";
    std::cout << "Audio pipeline: RTP -> jitter buffer -> ALAC -> PCM -> platform sink\n";
    std::cout << "Press Ctrl-C to stop.\n";

    while (receiver.running()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    return 0;
}
