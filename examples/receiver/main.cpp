#include "airplay2/airplay_receiver.h"

#include <chrono>
#include <iostream>
#include <thread>

int main() {
    gwl::airplay2::ReceiverConfig config;
    config.device_name = "GWL AirPlay";
    config.port = 7000;

    gwl::airplay2::AirPlayReceiver receiver;
    if (!receiver.start(config)) {
        std::cerr << "Failed to start GWL AirPlay receiver\n";
        return 1;
    }

    std::cout << "GWL AirPlay receiver started on port " << config.port << "\n";
    std::cout << "Current phase: HTTP/RTSP foundation; mDNS and media streaming are next.\n";
    std::cout << "Press Ctrl-C to stop.\n";

    while (receiver.running()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    return 0;
}
