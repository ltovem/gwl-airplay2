#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "airplay2/sdp.h"

namespace gwl::airplay2 {

struct CryptoParameters {
    std::vector<std::uint8_t> encrypted_key;
    std::vector<std::uint8_t> iv;
    std::string fingerprint;

    bool valid() const noexcept;
};

// Extracts the protocol parameters advertised in ANNOUNCE/SDP. This layer does
// not attempt to bypass pairing or recover protected session keys; it only
// validates and normalizes the parameters for the protocol state machine.
bool extract_crypto_parameters(const AirPlaySdp& sdp, CryptoParameters& result);

} // namespace gwl::airplay2
