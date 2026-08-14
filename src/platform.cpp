#include "airplay2/platform.h"

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

namespace gwl::airplay2 {

PlatformInfo current_platform() {
#if defined(__ANDROID__)
#if defined(__ANDROID_API__)
    return {Platform::Android, "Android", std::to_string(__ANDROID_API__)};
#else
    return {Platform::Android, "Android", {}};
#endif
#elif defined(__APPLE__)
#if TARGET_OS_TV
    return {Platform::TvOS, "tvOS", {}};
#elif TARGET_OS_IPHONE
    return {Platform::IOS, "iOS", {}};
#else
    return {Platform::MacOS, "macOS", {}};
#endif
#elif defined(_WIN32)
    return {Platform::Windows, "Windows", {}};
#elif defined(__FreeBSD__)
    return {Platform::FreeBSD, "FreeBSD", {}};
#elif defined(__linux__)
    return {Platform::Linux, "Linux", {}};
#else
    return {Platform::Unknown, "Unknown", {}};
#endif
}

std::unique_ptr<AudioSink> create_default_audio_sink() {
    // Native output backends deliberately live outside the protocol core.
    // Applications should inject an AudioSink appropriate to their UI/runtime.
    return nullptr;
}

} // namespace gwl::airplay2
