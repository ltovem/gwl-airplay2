#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "airplay2/alac_config.h"
#include "airplay2/audio_pipeline.h"
#include "airplay2/alac_rtp.h"

namespace gwl::airplay2 {

// End-to-end audio media bridge for unprotected ALAC RTP. It deliberately
// stops at the codec boundary: callers are responsible for any session-level
// media protection/decryption before passing a packet here.
class AlacAudioPipeline {
public:
    AlacAudioPipeline(std::unique_ptr<AlacDecoder> decoder,
                      std::unique_ptr<AudioSink> sink);
    ~AlacAudioPipeline();

    AlacAudioPipeline(const AlacAudioPipeline&) = delete;
    AlacAudioPipeline& operator=(const AlacAudioPipeline&) = delete;

    bool configure(const AlacConfig& config);
    bool push(const RtpPacket& packet);
    void reset();

    bool configured() const noexcept { return pipeline_ && pipeline_->opened(); }

private:
    std::unique_ptr<AudioPipeline> pipeline_;
};

} // namespace gwl::airplay2
