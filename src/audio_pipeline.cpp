#include "airplay2/audio_pipeline.h"

namespace gwl::airplay2 {

AudioPipeline::AudioPipeline(std::unique_ptr<AlacDecoder> decoder,
                             std::unique_ptr<AudioSink> sink)
    : decoder_(std::move(decoder)), sink_(std::move(sink)) {}

AudioPipeline::~AudioPipeline() { reset(); }

bool AudioPipeline::configure(const AlacConfig& config) {
    if (!decoder_ || !sink_ || !config.valid() || config.num_channels == 0 ||
        config.sample_rate == 0 || config.bit_depth != 16) {
        return false;
    }

    reset();
    if (!decoder_->configure(config)) return false;

    PcmFormat format;
    format.sample_rate = config.sample_rate;
    format.channels = config.num_channels;
    format.bits_per_sample = config.bit_depth;
    if (!sink_->open(format)) {
        decoder_->reset();
        return false;
    }

    format_ = format;
    opened_ = true;
    return true;
}

bool AudioPipeline::push(const std::vector<std::uint8_t>& packet) {
    if (!opened_ || !decoder_ || !sink_ || packet.empty()) return false;

    PcmAudioFrame pcm;
    if (!decoder_->decode(packet, pcm)) return false;
    if (pcm.channels != format_.channels || pcm.sample_rate != format_.sample_rate ||
        pcm.bits_per_sample != format_.bits_per_sample || pcm.frames == 0) {
        return false;
    }

    const std::size_t expected_samples =
        static_cast<std::size_t>(pcm.frames) * format_.channels;
    if (pcm.samples.size() != expected_samples) return false;

    return sink_->write(pcm.samples.data(), pcm.frames);
}

void AudioPipeline::reset() {
    if (sink_ && opened_) sink_->close();
    if (decoder_) decoder_->reset();
    opened_ = false;
}

} // namespace gwl::airplay2
