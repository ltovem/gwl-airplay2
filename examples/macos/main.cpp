#include "airplay2/airplay_receiver.h"
#include "airplay2/apple_audio_sink.h"

#include <chrono>
#include <csignal>
#include <iostream>
#include <memory>
#include <thread>

namespace {
volatile std::sig_atomic_t g_running = 1;
void handle_signal(int) { g_running = 0; }
}

int main() {
    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    gwl::airplay2::ReceiverConfig config;
    config.device_name = "GWL AirPlay Demo";
    config.enable_audio = true;
    config.enable_video = false;
    config.audio_sink_factory = [] {
        auto sink = std::make_unique<gwl::airplay2::AppleAudioSink>();
        if (!sink->open(44100, 2, 16)) return std::unique_ptr<gwl::airplay2::AudioSink>{};
        return std::unique_ptr<gwl::airplay2::AudioSink>(std::move(sink));
    };

    gwl::airplay2::AirPlayReceiver receiver;
    if (!receiver.start(config)) {
        std::cerr << "Failed to start GWL AirPlay macOS receiver\n";
        return 1;
    }

    std::cout << "GWL AirPlay macOS demo is running as '" << config.device_name << "'.\n";
    std::cout << "Select it from AirPlay on an Apple device to test audio reception.\n";
    std::cout << "Press Ctrl-C to stop.\n";

    while (g_running && receiver.running()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }

    receiver.stop();
    return 0;
}
