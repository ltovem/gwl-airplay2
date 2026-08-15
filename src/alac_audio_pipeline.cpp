#include "airplay2/alac_audio_pipeline.h"

namespace gwl::airplay2 {

AlacAudioPipeline::AlacAudioPipeline(std::unique_ptr<AlacDecoder> decoder,
                                     std::unique_ptr<AudioSink> sink)
    : pipeline_(std::make_unique<AudioPipeline>(std::move(decoder), std::move(sink))) {}

AlacAudioPipeline::~AlacAudioPipeline() = default;

bool AlacAudioPipeline::configure(const AlacConfig& config) {
    if (!pipeline_) return false;
    return pipeline_->configure(config);
}

bool AlacAudioPipeline::push(const RtpPacket& packet) {
    if (!pipeline_) return false;

    AlacRtpFrame frame;
    if (!AlacRtpPayload::extract(packet, frame) || frame.payload.empty()) return false;
    return pipeline_->push(frame.payload);
}

void AlacAudioPipeline::reset() {
    if (pipeline_) pipeline_->reset();
}

} // namespace gwl::airplay2
