#pragma once

#include <cstdint>

namespace gwl::airplay2 {

// Monotonic media clock used to schedule decoded audio without coupling the
// protocol core to CoreAudio, WASAPI, AAudio, ALSA, or another platform API.
class AudioClock {
public:
    using TimeNs = std::int64_t;

    static TimeNs now() noexcept;

    void reset() noexcept;
    void start(TimeNs media_timestamp, TimeNs wall_time_ns) noexcept;

    bool started() const noexcept { return started_; }
    TimeNs media_to_wall(TimeNs media_timestamp) const noexcept;

private:
    bool started_ = false;
    TimeNs media_origin_ = 0;
    TimeNs wall_origin_ = 0;
};

} // namespace gwl::airplay2
