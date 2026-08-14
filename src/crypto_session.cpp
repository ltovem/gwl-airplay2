#include "airplay2/crypto_session.h"

namespace gwl::airplay2 {

bool CryptoSession::configure(const CryptoParameters& parameters) {
    reset();
    if (!parameters.valid()) return false;

    parameters_ = parameters;
    // The SDP key/IV path is kept explicitly separate from modern paired
    // sessions. No protected-content decryption is performed here.
    mode_ = CryptoMode::LegacySdp;
    configured_ = true;
    return true;
}

void CryptoSession::reset() noexcept {
    mode_ = CryptoMode::None;
    configured_ = false;
    parameters_ = {};
}

} // namespace gwl::airplay2
