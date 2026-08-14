#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace gwl::airplay2 {

struct ReceiverConfig {
    std::string device_name = "GWL AirPlay";
    std::string device_id;
    std::uint16_t port = 7000;
    bool enable_audio = true;
    bool enable_video = true;
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
