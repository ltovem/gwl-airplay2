#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace gwl::airplay2 {

// AirPlay's legacy HKP pairing path used by iOS/macOS screen mirroring.
// /pair-setup exchanges the receiver's Ed25519 public key, then
// /pair-verify establishes an encrypted control session with Curve25519.
class AirPlayHkpPairing {
public:
    AirPlayHkpPairing();
    ~AirPlayHkpPairing();

    AirPlayHkpPairing(const AirPlayHkpPairing&) = delete;
    AirPlayHkpPairing& operator=(const AirPlayHkpPairing&) = delete;

    bool handle_pair_setup(const std::vector<std::uint8_t>& request,
                           std::vector<std::uint8_t>& response,
                           std::string& error);
    bool handle_pair_verify(const std::vector<std::uint8_t>& request,
                            std::vector<std::uint8_t>& response,
                            std::string& error);

    bool verified() const noexcept { return verified_; }

private:
    struct Impl;
    Impl* impl_;
    bool verified_ = false;
};

} // namespace gwl::airplay2
