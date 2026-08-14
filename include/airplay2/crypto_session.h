#pragma once

#include "airplay2/crypto.h"

#include <string>

namespace gwl::airplay2 {

enum class CryptoMode {
    None,
    LegacySdp,
    PairedSession
};

class CryptoSession {
public:
    bool configure(const CryptoParameters& parameters);
    void reset() noexcept;

    CryptoMode mode() const noexcept { return mode_; }
    bool configured() const noexcept { return configured_; }
    const CryptoParameters& parameters() const noexcept { return parameters_; }

private:
    CryptoMode mode_ = CryptoMode::None;
    bool configured_ = false;
    CryptoParameters parameters_{};
};

} // namespace gwl::airplay2
