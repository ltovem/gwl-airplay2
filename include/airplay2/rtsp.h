#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>

#include "airplay2/alac_audio_pipeline.h"
#include "airplay2/crypto_session.h"
#include "airplay2/http_server.h"
#include "airplay2/rtp.h"
#include "airplay2/rtp_jitter_buffer.h"
#include "airplay2/sdp.h"

namespace gwl::airplay2 {

struct RtspRequest {
    std::string method;
    std::string uri;
    int cseq = 0;
    std::map<std::string, std::string> headers;
    std::string body;
};

struct RtspResponse {
    int status = 200;
    std::string reason = "OK";
    std::map<std::string, std::string> headers;
    std::string body;
};

struct RtpTransport {
    std::uint16_t client_control_port = 0;
    std::uint16_t client_timing_port = 0;
    std::uint16_t server_control_port = 0;
    std::uint16_t server_timing_port = 0;
    std::uint16_t server_data_port = 0;
    std::uint16_t event_port = 0;
};

class RtspSession {
public:
    using MediaPacketHandler = std::function<void(const RtpPacket&)>;
    using LogHandler = std::function<void(const std::string&)>;

    RtspSession();
    ~RtspSession();

    RtspSession(const RtspSession&) = delete;
    RtspSession& operator=(const RtspSession&) = delete;

    RtspResponse handle(const RtspRequest& request);
    void reset();

    bool configured() const noexcept { return configured_; }
    bool recording() const noexcept { return recording_; }
    const RtpTransport& transport() const noexcept { return transport_; }
    const AirPlaySdp& sdp() const noexcept { return sdp_; }
    const CryptoSession& crypto() const noexcept { return crypto_; }
    const RtpStreamStats& media_stats() const noexcept { return jitter_buffer_.stats(); }
    RtpReceiver* media_receiver() noexcept { return media_receiver_.get(); }

    void set_media_packet_handler(MediaPacketHandler handler);
    void clear_media_packet_handler();
    void set_log_handler(LogHandler handler) { log_handler_ = std::move(handler); }

    void set_alac_audio_pipeline(std::unique_ptr<AlacAudioPipeline> pipeline);
    bool audio_pipeline_configured() const noexcept {
        return alac_audio_pipeline_ && alac_audio_pipeline_->configured();
    }

private:
    RtspResponse options(const RtspRequest& request);
    RtspResponse announce(const RtspRequest& request);
    RtspResponse setup(const RtspRequest& request);
    RtspResponse record(const RtspRequest& request);
    RtspResponse pause(const RtspRequest& request);
    RtspResponse flush(const RtspRequest& request);
    RtspResponse get_parameter(const RtspRequest& request);
    RtspResponse set_parameter(const RtspRequest& request);
    RtspResponse teardown(const RtspRequest& request);

    void handle_media_packet(const RtpPacket& packet);
    void log(const std::string& message) const;
    bool start_event_channel();
    void stop_event_channel();
    HttpHandler make_event_handler();

    bool configured_ = false;
    bool setup_info_complete_ = false;
    bool recording_ = false;
    RtpTransport transport_{};
    AirPlaySdp sdp_{};
    CryptoSession crypto_{};
    std::unique_ptr<RtpReceiver> media_receiver_;
    std::unique_ptr<RtpReceiver> control_receiver_;
    std::unique_ptr<RtpReceiver> timing_receiver_;
    std::unique_ptr<HttpServer> event_server_;
    RtpJitterBuffer jitter_buffer_{};
    MediaPacketHandler media_packet_handler_;
    LogHandler log_handler_;
    std::unique_ptr<AlacAudioPipeline> alac_audio_pipeline_;
    std::uint64_t received_packets_ = 0;
    std::uint64_t received_bytes_ = 0;
};

} // namespace gwl::airplay2
