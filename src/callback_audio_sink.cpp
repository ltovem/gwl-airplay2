#include "airplay2/callback_audio_sink.h"

#include <utility>

namespace gwl::airplay2 {

CallbackAudioSink::CallbackAudioSink(PcmWriteCallback callback)
    : callback_(std::move(callback)) {}

bool CallbackAudioSink::open(const PcmFormat& format) {
    if (!callback_ || format.sample_rate == 0 || format.channels == 0 ||
        format.bits_per_sample != 16) {
        return false;
    }
    format_ = format;
    opened_ = true;
    return true;
}

void CallbackAudioSink::close() {
    opened_ = false;
}

bool CallbackAudioSink::write(const std::int16_t* samples, std::size_t frames) {
    if (!opened_ || !callback_ || (frames != 0 && samples == nullptr)) return false;
    return callback_(samples, frames, format_);
}

} // namespace gwl::airplay2
