#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace gwl::airplay2 {

// AirPlay 2 transient pairing uses the HomeKit SRP flow with the well-known
// screenless-device setup code 3939.  The protocol layer is deliberately
// independent from the HTTP transport so the same state machine can be used
// on desktop and mobile targets.
class AirPlayTransientPairing {
public:
    AirPlayTransientPairing();
    ~AirPlayTransientPairing();

    AirPlayTransientPairing(const AirPlayTransientPairing&) = delete;
    AirPlayTransientPairing& operator=(const AirPlayTransientPairing&) = delete;

    // Returns an RTSP response body for /pair-setup, or an empty vector on
    // malformed/unsupported input.  state is advanced internally (M1-M4).
    bool handle(const std::vector<std::uint8_t>& request,
                std::vector<std::uint8_t>& response,
                std::string& error);

    bool complete() const noexcept { return complete_; }
    const std::vector<std::uint8_t>& shared_secret() const noexcept { return shared_secret_; }

    void reset();

private:
    struct Impl;
    Impl* impl_;
    bool complete_ = false;
    std::vector<std::uint8_t> shared_secret_;
};

} // namespace gwl::airplay2
