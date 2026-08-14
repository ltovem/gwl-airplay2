#include "airplay2/audio_pipeline.h"

namespace gwl::airplay2 {

AudioPipeline::AudioPipeline(std::unique_ptr<AudioDecoder> decoder,
                             std::unique_ptr<AudioSink> sink)
    : decoder_(std::move(decoder)), sink_(std::move(sink)) {}

AudioPipeline::~AudioPipeline() { reset(); }

bool AudioPipeline::configure(const PcmFormat& format,
                              const std::vector<std::uint8_t>& codec_data) {
    if (!decoder_ || !sink_) return false;
    reset();
    if (!decoder_->configure(codec_data)) return false;
    if (!sink_->open(format)) return false;
    opened_ = true;
    return true;
}

bool AudioPipeline::push(const std::uint8_t* data, std::size_t size) {
    if (!opened_ || !decoder_ || !sink_ || (!data && size != 0)) return false;
    std::vector<std::int16_t> pcm;
    if (!decoder_->decode(data, size, pcm)) return false;
    if (pcm.empty()) return true;
    // PCM is interleaved. The sink receives frame count, not sample count.
    // The configured channel count is owned by the sink implementation.
    return sink_->write(pcm.data(), pcm.size());
}

void AudioPipeline::reset() {
    if (sink_ && opened_) sink_->close();
    opened_ = false;
}

} // namespace gwl::airplay2
