#include "airplay2/alac_decoder.h"

namespace gwl::airplay2 {

bool NullAlacDecoder::configure(const AlacConfig& config) {
    if (!config.valid()) return false;
    config_ = config;
    configured_ = true;
    return true;
}

bool NullAlacDecoder::decode(const std::vector<std::uint8_t>&, PcmAudioFrame& output) {
    // Deliberately does not pretend to decode compressed ALAC data. This backend
    // is useful for wiring and tests until a real software decoder is selected.
    output = {};
    return false;
}

void NullAlacDecoder::reset() {
    config_ = {};
    configured_ = false;
}

std::unique_ptr<AlacDecoder> create_null_alac_decoder() {
    return std::make_unique<NullAlacDecoder>();
}

} // namespace gwl::airplay2
