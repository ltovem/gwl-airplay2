#include "airplay2/alac_config.h"
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
    cookie[8] = 2;    // stereo
    cookie[19] = 0x00;
    cookie[20] = 0x00;
    cookie[21] = 0xAC;
    cookie[22] = 0x44;
    cookie[23] = 0x00; // 44100 Hz

    AlacConfig config;
    assert(parse_alac_config(cookie, config));
    assert(config.frame_length == 4096);
    assert(config.num_channels == 2);
    assert(config.sample_rate == 44100);

    assert(parse_alac_fmtp("sampleRate=44100;channels=2;bitDepth=16;frameLength=4096", config));
    assert(config.valid());
    assert(config.bit_depth == 16);
}

class TestDecoder final : public AudioDecoder {
public:
    bool configure(const std::vector<std::uint8_t>&) override { return true; }

    bool decode(const std::uint8_t*, std::size_t, std::vector<std::int16_t>& pcm) override {
        pcm = {1, 2, 3, 4}; // two stereo frames
        return true;
    }
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

    PcmFormat format;
    format.sample_rate = 44100;
    format.channels = 2;
    assert(pipeline.configure(format, {}));
    assert(pipeline.push(nullptr, 0));
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
