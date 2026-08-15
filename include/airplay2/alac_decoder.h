#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "airplay2/alac_config.h"

namespace gwl::airplay2 {

struct PcmAudioFrame {
    std::uint32_t sample_rate = 0;
    std::uint16_t channels = 0;
    std::uint16_t bits_per_sample = 0;
    std::uint32_t frames = 0;
    std::vector<std::int16_t> samples;
};

class AlacDecoder {
public:
    virtual ~AlacDecoder() = default;
    virtual bool configure(const AlacConfig& config) = 0;
    virtual bool decode(const std::vector<std::uint8_t>& packet, PcmAudioFrame& output) = 0;
    virtual void reset() = 0;
};

class NullAlacDecoder final : public AlacDecoder {
public:
    bool configure(const AlacConfig& config) override;
    bool decode(const std::vector<std::uint8_t>& packet, PcmAudioFrame& output) override;
    void reset() override;
private:
    AlacConfig config_{};
    bool configured_ = false;
};

// Portable software decoder backed by Apple's Apache-2.0 ALAC reference code.
class SoftwareAlacDecoder final : public AlacDecoder {
public:
    SoftwareAlacDecoder();
    ~SoftwareAlacDecoder() override;
    SoftwareAlacDecoder(const SoftwareAlacDecoder&) = delete;
    SoftwareAlacDecoder& operator=(const SoftwareAlacDecoder&) = delete;

    bool configure(const AlacConfig& config) override;
    bool decode(const std::vector<std::uint8_t>& packet, PcmAudioFrame& output) override;
    void reset() override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

std::unique_ptr<AlacDecoder> create_null_alac_decoder();
std::unique_ptr<AlacDecoder> create_software_alac_decoder();

} // namespace gwl::airplay2
