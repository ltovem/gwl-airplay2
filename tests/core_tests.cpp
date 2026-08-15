#include "airplay2/alac_config.h"
#include "airplay2/alac_decoder.h"
#include "airplay2/audio_pipeline.h"
#include "airplay2/rtp_jitter_buffer.h"

#include <cassert>
#include <cstdint>
#include <memory>
#include <vector>

using namespace gwl::airplay2;

static RtpPacket packet(std::uint16_t sequence) {
    RtpPacket p;
    p.sequence = sequence;
    p.timestamp = static_cast<std::uint32_t>(sequence) * 352;
    p.payload = {0x01, 0x02, 0x03};
    return p;
}

static void test_jitter_reordering() {
    RtpJitterBuffer buffer(8);
    assert(buffer.push(packet(100)));
    assert(buffer.push(packet(102)));
    assert(buffer.push(packet(101)));

    auto first = buffer.pop();
    auto second = buffer.pop();
    auto third = buffer.pop();
    assert(first && first->sequence == 100);
    assert(second && second->sequence == 101);
    assert(third && third->sequence == 102);
    assert(buffer.stats().reordered == 1);
}

static void test_jitter_wraparound() {
    RtpJitterBuffer buffer(8);
    assert(buffer.push(packet(65535)));
    assert(buffer.push(packet(0)));

    auto first = buffer.pop();
    auto second = buffer.pop();
    assert(first && first->sequence == 65535);
    assert(second && second->sequence == 0);
}

static void test_jitter_duplicates() {
    RtpJitterBuffer buffer(8);
    assert(buffer.push(packet(10)));
    assert(!buffer.push(packet(10)));
    assert(buffer.stats().discarded == 1);
    auto value = buffer.pop();
    assert(value && value->sequence == 10);
    assert(!buffer.pop());
}

static void test_alac_config_and_fmtp() {
    std::vector<std::uint8_t> cookie(24, 0);
    cookie[0] = 0x00;
    cookie[1] = 0x00;
    cookie[2] = 0x10;
    cookie[3] = 0x00; // 4096 samples/frame
    cookie[5] = 16;   // 16-bit samples
    cookie[9] = 2;    // stereo
    cookie[20] = 0x00;
    cookie[21] = 0x00;
    cookie[22] = 0xAC;
    cookie[23] = 0x44; // 44100 Hz

    AlacConfig config;
    assert(parse_alac_config(cookie, config));
    assert(config.frame_length == 4096);
    assert(config.num_channels == 2);
    assert(config.bit_depth == 16);
    assert(config.sample_rate == 44100);

    assert(parse_alac_fmtp("sampleRate=44100;channels=2;bitDepth=16;frameLength=4096", config));
    assert(config.valid());
    assert(config.bit_depth == 16);
}

class TestDecoder final : public AlacDecoder {
public:
    bool configure(const AlacConfig& config) override {
        configured = config.valid();
        return configured;
    }

    bool decode(const std::vector<std::uint8_t>& packet, PcmAudioFrame& pcm) override {
        if (!configured || packet.empty()) return false;
        pcm.sample_rate = 44100;
        pcm.channels = 2;
        pcm.bits_per_sample = 16;
        pcm.frames = 2;
        pcm.samples = {1, 2, 3, 4}; // two stereo frames
        return true;
    }

    void reset() override { configured = false; }

    bool configured = false;
};

class TestSink final : public AudioSink {
public:
    bool open(const PcmFormat& format) override {
        opened = true;
        channels = format.channels;
        return true;
    }

    void close() override { opened = false; }

    bool write(const std::int16_t* samples, std::size_t frames) override {
        assert(opened);
        assert(samples != nullptr);
        received_frames = frames;
        return true;
    }

    bool opened = false;
    std::uint16_t channels = 0;
    std::size_t received_frames = 0;
};

static void test_audio_pipeline_frame_count() {
    auto sink = std::make_unique<TestSink>();
    auto* sink_ptr = sink.get();
    AudioPipeline pipeline(std::make_unique<TestDecoder>(), std::move(sink));

    AlacConfig config;
    config.frame_length = 4096;
    config.bit_depth = 16;
    config.num_channels = 2;
    config.sample_rate = 44100;
    assert(config.valid());
    assert(pipeline.configure(config));
    assert(pipeline.push({0x10}));
    assert(sink_ptr->received_frames == 2);
    pipeline.reset();
    assert(!sink_ptr->opened);
}

int main() {
    test_jitter_reordering();
    test_jitter_wraparound();
    test_jitter_duplicates();
    test_alac_config_and_fmtp();
    test_audio_pipeline_frame_count();
    return 0;
}
