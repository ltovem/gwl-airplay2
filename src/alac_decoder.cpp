#include "airplay2/alac_decoder.h"

#include <algorithm>
#include <limits>

#include "ALACBitUtilities.h"
#include "ALACDecoder.h"

namespace gwl::airplay2 {

namespace {

std::vector<std::uint8_t> make_cookie(const AlacConfig& c) {
    if (!c.codec_data.empty()) return c.codec_data;
    std::vector<std::uint8_t> cookie(24, 0);
    auto put32 = [&](std::size_t p, std::uint32_t v) {
        cookie[p + 0] = static_cast<std::uint8_t>(v >> 24);
        cookie[p + 1] = static_cast<std::uint8_t>(v >> 16);
        cookie[p + 2] = static_cast<std::uint8_t>(v >> 8);
        cookie[p + 3] = static_cast<std::uint8_t>(v);
    };
    auto put16 = [&](std::size_t p, std::uint16_t v) {
        cookie[p + 0] = static_cast<std::uint8_t>(v >> 8);
        cookie[p + 1] = static_cast<std::uint8_t>(v);
    };
    put32(0, c.frame_length);
    cookie[4] = c.compatible_version;
    cookie[5] = static_cast<std::uint8_t>(c.bit_depth);
    cookie[6] = c.pb;
    cookie[7] = c.mb;
    cookie[8] = c.kb;
    cookie[9] = c.num_channels;
    put16(10, c.max_run);
    put32(12, c.max_frame_bytes);
    put32(16, c.avg_bit_rate);
    put32(20, c.sample_rate);
    return cookie;
}

} // namespace

struct SoftwareAlacDecoder::Impl {
    ALACDecoder decoder;
    AlacConfig config{};
    bool configured = false;
};

SoftwareAlacDecoder::SoftwareAlacDecoder() : impl_(std::make_unique<Impl>()) {}
SoftwareAlacDecoder::~SoftwareAlacDecoder() = default;

bool SoftwareAlacDecoder::configure(const AlacConfig& config) {
    if (!impl_ || !config.valid() || config.bit_depth != 16 || config.num_channels == 0 ||
        config.num_channels > 8 || config.frame_length == 0 || config.frame_length > (1u << 20)) {
        return false;
    }
    const auto cookie = make_cookie(config);
    if (cookie.size() < 24 || impl_->decoder.Init(const_cast<std::uint8_t*>(cookie.data()),
                                                  static_cast<std::uint32_t>(cookie.size())) != 0) {
        return false;
    }
    impl_->config = config;
    impl_->configured = true;
    return true;
}

bool SoftwareAlacDecoder::decode(const std::vector<std::uint8_t>& packet, PcmAudioFrame& output) {
    output = {};
    if (!impl_ || !impl_->configured || packet.empty()) return false;

    if (impl_->config.frame_length > std::numeric_limits<std::uint32_t>::max() /
                                      impl_->config.num_channels / sizeof(std::int16_t)) {
        return false;
    }
    const std::size_t capacity = static_cast<std::size_t>(impl_->config.frame_length) *
                                 impl_->config.num_channels;
    std::vector<std::int16_t> samples(capacity);
    BitBuffer bits{};
    BitBufferInit(&bits, const_cast<std::uint8_t*>(packet.data()), static_cast<std::uint32_t>(packet.size()));

    std::uint32_t decoded = 0;
    if (impl_->decoder.Decode(&bits,
                              reinterpret_cast<std::uint8_t*>(samples.data()),
                              impl_->config.frame_length,
                              impl_->config.num_channels,
                              &decoded) != 0 || decoded == 0) {
        return false;
    }

    samples.resize(static_cast<std::size_t>(decoded) * impl_->config.num_channels);
    output.sample_rate = impl_->config.sample_rate;
    output.channels = impl_->config.num_channels;
    output.bits_per_sample = 16;
    output.frames = decoded;
    output.samples = std::move(samples);
    return true;
}

void SoftwareAlacDecoder::reset() {
    if (impl_) {
        impl_->config = {};
        impl_->configured = false;
        impl_->decoder.~ALACDecoder();
        new (&impl_->decoder) ALACDecoder();
    }
}

std::unique_ptr<AlacDecoder> create_software_alac_decoder() {
    return std::make_unique<SoftwareAlacDecoder>();
}

bool NullAlacDecoder::configure(const AlacConfig& config) {
    if (!config.valid()) return false;
    config_ = config;
    configured_ = true;
    return true;
}

bool NullAlacDecoder::decode(const std::vector<std::uint8_t>&, PcmAudioFrame& output) {
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
