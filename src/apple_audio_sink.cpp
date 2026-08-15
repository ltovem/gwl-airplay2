#include "airplay2/apple_audio_sink.h"

#if defined(__APPLE__)

#include <AudioToolbox/AudioToolbox.h>
#include <TargetConditionals.h>

#include <algorithm>
#include <cstring>
#include <mutex>
#include <vector>

namespace gwl::airplay2 {

struct AppleAudioSink::Impl {
    AudioUnit unit = nullptr;
    PcmFormat format{};
    std::vector<std::int16_t> ring;
    std::size_t read_pos = 0;
    std::size_t write_pos = 0;
    std::size_t available = 0;
    std::mutex mutex;
    bool opened = false;

    static OSStatus render(void* refcon,
                           AudioUnitRenderActionFlags*,
                           const AudioTimeStamp*,
                           UInt32,
                           UInt32 frames,
                           AudioBufferList* io_data) {
        auto* self = static_cast<Impl*>(refcon);
        if (!self || !io_data || io_data->mNumberBuffers == 0) return noErr;

        const std::size_t channels = self->format.channels;
        const std::size_t requested = static_cast<std::size_t>(frames) * channels;
        std::vector<std::int16_t> temp(requested, 0);

        {
            std::lock_guard<std::mutex> lock(self->mutex);
            const std::size_t take = std::min(requested, self->available);
            for (std::size_t i = 0; i < take; ++i) {
                temp[i] = self->ring[self->read_pos];
                self->read_pos = (self->read_pos + 1) % self->ring.size();
            }
            self->available -= take;
        }

        if (io_data->mNumberBuffers == 1) {
            const std::size_t bytes = requested * sizeof(std::int16_t);
            const std::size_t capacity = io_data->mBuffers[0].mDataByteSize;
            const std::size_t copy_bytes = std::min(bytes, capacity);
            std::memcpy(io_data->mBuffers[0].mData, temp.data(), copy_bytes);
            io_data->mBuffers[0].mDataByteSize = static_cast<UInt32>(copy_bytes);
        } else {
            for (UInt32 b = 0; b < io_data->mNumberBuffers; ++b) {
                std::memset(io_data->mBuffers[b].mData, 0, io_data->mBuffers[b].mDataByteSize);
            }
        }
        return noErr;
    }
};

AppleAudioSink::AppleAudioSink() : impl_(std::make_unique<Impl>()) {}

AppleAudioSink::~AppleAudioSink() { close(); }

bool AppleAudioSink::open(const PcmFormat& format) {
    close();
    if (!impl_ || format.channels == 0 || format.channels > 8 || format.sample_rate == 0 ||
        format.bits_per_sample != 16) return false;

    impl_->format = format;
    impl_->ring.assign(static_cast<std::size_t>(format.sample_rate) * format.channels * 2, 0);

    AudioComponentDescription desc{};
    desc.componentType = kAudioUnitType_Output;
#if TARGET_OS_OSX
    desc.componentSubType = kAudioUnitSubType_DefaultOutput;
#else
    desc.componentSubType = kAudioUnitSubType_RemoteIO;
#endif
    desc.componentManufacturer = kAudioUnitManufacturer_Apple;

    AudioComponent component = AudioComponentFindNext(nullptr, &desc);
    if (!component || AudioComponentInstanceNew(component, &impl_->unit) != noErr) {
        impl_->unit = nullptr;
        return false;
    }

    AudioStreamBasicDescription asbd{};
    asbd.mSampleRate = static_cast<Float64>(format.sample_rate);
    asbd.mFormatID = kAudioFormatLinearPCM;
    asbd.mFormatFlags = kAudioFormatFlagIsSignedInteger | kAudioFormatFlagsNativeEndian | kAudioFormatFlagIsPacked;
    asbd.mBytesPerPacket = format.channels * sizeof(std::int16_t);
    asbd.mFramesPerPacket = 1;
    asbd.mBytesPerFrame = format.channels * sizeof(std::int16_t);
    asbd.mChannelsPerFrame = format.channels;
    asbd.mBitsPerChannel = 16;

    if (AudioUnitSetProperty(impl_->unit, kAudioUnitProperty_StreamFormat,
                             kAudioUnitScope_Input, 0, &asbd, sizeof(asbd)) != noErr) {
        close();
        return false;
    }

    AURenderCallbackStruct callback{};
    callback.inputProc = &Impl::render;
    callback.inputProcRefCon = impl_.get();
    if (AudioUnitSetProperty(impl_->unit, kAudioUnitProperty_SetRenderCallback,
                             kAudioUnitScope_Input, 0, &callback, sizeof(callback)) != noErr) {
        close();
        return false;
    }

    if (AudioUnitInitialize(impl_->unit) != noErr || AudioOutputUnitStart(impl_->unit) != noErr) {
        close();
        return false;
    }

    impl_->opened = true;
    return true;
}

void AppleAudioSink::close() {
    if (!impl_) return;
    if (impl_->unit) {
        AudioOutputUnitStop(impl_->unit);
        AudioUnitUninitialize(impl_->unit);
        AudioComponentInstanceDispose(impl_->unit);
        impl_->unit = nullptr;
    }
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->ring.clear();
    impl_->read_pos = impl_->write_pos = impl_->available = 0;
    impl_->opened = false;
}

bool AppleAudioSink::write(const std::int16_t* samples, std::size_t frames) {
    if (!impl_ || !impl_->opened || (!samples && frames != 0)) return false;
    const std::size_t count = frames * impl_->format.channels;
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (count > impl_->ring.size() - impl_->available) return false;
    for (std::size_t i = 0; i < count; ++i) {
        impl_->ring[impl_->write_pos] = samples[i];
        impl_->write_pos = (impl_->write_pos + 1) % impl_->ring.size();
    }
    impl_->available += count;
    return true;
}

} // namespace gwl::airplay2

#else
#error "apple_audio_sink.cpp must only be built on Apple platforms"
#endif
