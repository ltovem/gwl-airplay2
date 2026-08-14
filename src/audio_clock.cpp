#include "airplay2/audio_clock.h"

#include <chrono>

namespace gwl::airplay2 {

AudioClock::TimeNs AudioClock::now() noexcept {
    using clock = std::chrono::steady_clock;
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        clock::now().time_since_epoch()).count();
}

void AudioClock::reset() noexcept {
    started_ = false;
    media_origin_ = 0;
    wall_origin_ = 0;
}

void AudioClock::start(TimeNs media_timestamp, TimeNs wall_time_ns) noexcept {
    media_origin_ = media_timestamp;
    wall_origin_ = wall_time_ns;
    started_ = true;
}

AudioClock::TimeNs AudioClock::media_to_wall(TimeNs media_timestamp) const noexcept {
    if (!started_) return 0;
    return wall_origin_ + (media_timestamp - media_origin_);
}

} // namespace gwl::airplay2
