#include "airplay2/platform_audio.h"

#include <cstddef>
#include <cstdint>

namespace gwl::airplay2 {
namespace {

class NullAudioSink final : public AudioSink {
public:
    bool open(const PcmFormat& format) override {
        format_ = format;
        opened_ = true;
        return true;
    }

    void close() override { opened_ = false; }

    bool write(const std::int16_t*, std::size_t) override {
        return opened_;
    }

private:
    PcmFormat format_{};
    bool opened_ = false;
};

} // namespace

std::unique_ptr<AudioSink> create_platform_audio_sink() {
    // Keep the core library dependency-free. Desktop/mobile applications can
    // replace this sink with CoreAudio, AVAudioEngine, WASAPI, AAudio/Oboe,
    // PipeWire/ALSA, or another native output implementation.
    return std::make_unique<NullAudioSink>();
}

std::string platform_audio_backend_name() {
#if defined(__APPLE__)
#if defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE
    return "apple-mobile-adapter";
#else
    return "apple-desktop-adapter";
#endif
#elif defined(_WIN32)
    return "windows-adapter";
#elif defined(__ANDROID__)
    return "android-adapter";
#elif defined(__linux__)
    return "linux-adapter";
#elif defined(__FreeBSD__)
    return "freebsd-adapter";
#else
    return "portable-adapter";
#endif
}

} // namespace gwl::airplay2
