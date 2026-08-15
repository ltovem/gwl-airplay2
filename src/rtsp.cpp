#include "airplay2/rtsp.h"
#include "airplay2/alac_config.h"
#include "airplay2/crypto.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <sstream>

namespace gwl::airplay2 {
namespace {

RtspResponse base_response(const RtspRequest& request, int status, std::string reason) {
    RtspResponse response;
    response.status = status;
    response.reason = std::move(reason);
    if (request.cseq > 0) response.headers["CSeq"] = std::to_string(request.cseq);
    response.headers["Server"] = "GWL-AirPlay2/0.1";
    return response;
}

std::string header_value(const RtspRequest& request, const char* name) {
    for (const auto& item : request.headers) {
        if (item.first.size() != std::char_traits<char>::length(name)) continue;
        bool equal = true;
        for (std::size_t i = 0; i < item.first.size(); ++i) {
            if (std::tolower(static_cast<unsigned char>(item.first[i])) !=
                std::tolower(static_cast<unsigned char>(name[i]))) { equal = false; break; }
        }
        if (equal) return item.second;
    }
    return {};
}

std::uint16_t parse_port(const std::string& text, const char* key) {
    const auto pos = text.find(key);
    if (pos == std::string::npos) return 0;
    const auto start = pos + std::char_traits<char>::length(key);
    auto end = start;
    while (end < text.size() && std::isdigit(static_cast<unsigned char>(text[end]))) ++end;
    if (end == start) return 0;
    try {
        const auto value = std::stoul(text.substr(start, end - start));
        return value <= 65535 ? static_cast<std::uint16_t>(value) : 0;
    } catch (...) { return 0; }
}

bool is_binary_plist(const std::string& body) {
    return body.size() >= 8 && body.compare(0, 8, "bplist00") == 0;
}

std::string info_response_plist() {
    static const std::array<unsigned char, 69> bytes = {
        0x62,0x70,0x6c,0x69,0x73,0x74,0x30,0x30,0xd1,0x01,0x02,0x5d,0x69,0x6e,0x69,
        0x74,0x69,0x61,0x6c,0x56,0x6f,0x6c,0x75,0x6d,0x65,0x13,0xff,0xff,0xff,0xff,0xff,
        0xff,0x88,0x08,0x0b,0x19,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x01,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00,0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0x00,0x00,0x22
    };
    return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

std::string setup_event_response_plist(std::uint16_t event_port, std::uint16_t timing_port) {
    // Binary plist for {eventPort: <port>, timingPort: <port>}.
    // Ports are encoded as 16-bit values inside 32-bit integer objects.
    std::array<unsigned char, 78> bytes = {
        0x62,0x70,0x6c,0x69,0x73,0x74,0x30,0x30,
        0xd2,0x01,0x02,0x03,0x04,
        0x59,0x65,0x76,0x65,0x6e,0x74,0x50,0x6f,0x72,0x74,
        0x5a,0x74,0x69,0x6d,0x69,0x6e,0x67,0x50,0x6f,0x72,0x74,
        0x11,0x00,0x00,0x00,0x00,
        0x11,0x00,0x00,0x00,0x00,
        0x08,0x0d,0x17,0x22,0x25,
        0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x01,
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x05,
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00,0x00,0x28
    };
    bytes[35] = static_cast<unsigned char>((event_port >> 8) & 0xff);
    bytes[36] = static_cast<unsigned char>(event_port & 0xff);
    bytes[38] = static_cast<unsigned char>((timing_port >> 8) & 0xff);
    bytes[39] = static_cast<unsigned char>(timing_port & 0xff);
    return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

} // namespace

RtspSession::RtspSession() = default;
RtspSession::~RtspSession() = default;

void RtspSession::log(const std::string& message) const {
    if (log_handler_) log_handler_(message);
}

void RtspSession::reset() {
    recording_ = false;
    configured_ = false;
    setup_info_complete_ = false;
    media_packet_handler_ = {};
    if (media_receiver_) media_receiver_->clear_packet_handler();
    if (control_receiver_) control_receiver_->clear_packet_handler();
    if (timing_receiver_) timing_receiver_->clear_packet_handler();
    if (media_receiver_) media_receiver_->close();
    if (control_receiver_) control_receiver_->close();
    if (timing_receiver_) timing_receiver_->close();
    media_receiver_.reset();
    control_receiver_.reset();
    timing_receiver_.reset();
    jitter_buffer_.reset();
    transport_ = {};
    sdp_ = {};
    crypto_.reset();
    received_packets_ = 0;
    received_bytes_ = 0;
    if (alac_audio_pipeline_) alac_audio_pipeline_->reset();
}

RtspResponse RtspSession::handle(const RtspRequest& request) {
    if (request.method == "OPTIONS") return options(request);
    if (request.method == "ANNOUNCE") return announce(request);
    if (request.method == "SETUP") return setup(request);
    if (request.method == "RECORD") return record(request);
    if (request.method == "PAUSE") return pause(request);
    if (request.method == "FLUSH") return flush(request);
    if (request.method == "GET_PARAMETER") return get_parameter(request);
    if (request.method == "SET_PARAMETER") return set_parameter(request);
    if (request.method == "TEARDOWN") return teardown(request);
    return base_response(request, 405, "Method Not Allowed");
}

RtspResponse RtspSession::options(const RtspRequest& request) {
    auto response = base_response(request, 200, "OK");
    response.headers["Public"] = "OPTIONS, ANNOUNCE, SETUP, RECORD, PAUSE, FLUSH, TEARDOWN, GET_PARAMETER, SET_PARAMETER";
    response.headers["Allow"] = response.headers["Public"];
    return response;
}

RtspResponse RtspSession::announce(const RtspRequest& request) {
    AirPlaySdp parsed;
    if (!parse_sdp(request.body, parsed)) return base_response(request, 400, "Bad Request");
    if (!parsed.has_audio() && !parsed.has_video()) return base_response(request, 415, "Unsupported Media Type");

    sdp_ = parsed;
    configured_ = true;

    log("AirPlay ANNOUNCE accepted");
    for (const auto& media : parsed.media) {
        std::ostringstream description;
        description << "SDP media: " << media.type
                    << " payload=" << media.payload_type
                    << " codec=" << (media.codec.empty() ? "unknown" : media.codec)
                    << " clock=" << media.clock_rate;
        log(description.str());
    }
    log(parsed.has_video()
        ? "Screen Mirroring: video SDP detected"
        : "Screen Mirroring: no video SDP in this session");

    if (alac_audio_pipeline_ && parsed.has_audio() && !parsed.fmtp.empty()) {
        AlacConfig config;
        if (parse_alac_fmtp(parsed.fmtp, config) && config.valid()) {
            if (!alac_audio_pipeline_->configure(config)) {
                return base_response(request, 415, "Unsupported Media Type");
            }
        }
    }

    CryptoParameters parameters;
    if (extract_crypto_parameters(parsed, parameters)) {
        if (!crypto_.configure(parameters)) return base_response(request, 400, "Bad Request");
        log("RTSP crypto parameters detected");
    }

    auto response = base_response(request, 200, "OK");
    response.headers["Content-Length"] = "0";
    return response;
}

RtspResponse RtspSession::setup(const RtspRequest& request) {
    if (!setup_info_complete_ && is_binary_plist(request.body)) {
        setup_info_complete_ = true;
        configured_ = true;

        // Allocate all three UDP endpoints before replying. Modern AirPlay 2
        // senders use the ports advertised in this plist to decide whether
        // the receiver is ready for the subsequent media SETUP/RECORD phase.
        if (!media_receiver_) media_receiver_ = std::make_unique<RtpReceiver>();
        if (!control_receiver_) control_receiver_ = std::make_unique<RtpReceiver>();
        if (!timing_receiver_) timing_receiver_ = std::make_unique<RtpReceiver>();

        if (!media_receiver_->bind(0) || !control_receiver_->bind(0) || !timing_receiver_->bind(0)) {
            log("AirPlay SETUP: failed to allocate RTP/control/timing UDP ports");
            return base_response(request, 500, "Internal Server Error");
        }

        transport_.server_data_port = media_receiver_->port();
        transport_.server_control_port = control_receiver_->port();
        transport_.server_timing_port = timing_receiver_->port();

        log("AirPlay SETUP info accepted (binary plist; no legacy ANNOUNCE)");
        std::ostringstream ports;
        ports << "AirPlay transport allocated: data=" << transport_.server_data_port
              << " control=" << transport_.server_control_port
              << " timing=" << transport_.server_timing_port;
        log(ports.str());

        auto response = base_response(request, 200, "OK");
        response.body = setup_event_response_plist(transport_.server_control_port,
                                                   transport_.server_timing_port);
        response.headers["Content-Length"] = std::to_string(response.body.size());
        response.headers["Content-Type"] = "application/x-apple-binary-plist";
        return response;
    }

    if (!configured_) return base_response(request, 455, "Method Not Valid in This State");

    const auto transport = header_value(request, "Transport");
    transport_.client_control_port = parse_port(transport, "control_port=");
    transport_.client_timing_port = parse_port(transport, "timing_port=");

    if (!media_receiver_) media_receiver_ = std::make_unique<RtpReceiver>();
    if (!media_receiver_->running() && !media_receiver_->bind(0)) {
        return base_response(request, 500, "Internal Server Error");
    }

    media_receiver_->set_packet_handler([this](const RtpPacket& packet) {
        handle_media_packet(packet);
    });

    transport_.server_data_port = media_receiver_->port();
    if (transport_.server_control_port == 0) {
        if (!control_receiver_) control_receiver_ = std::make_unique<RtpReceiver>();
        if (!control_receiver_->running() && !control_receiver_->bind(0)) return base_response(request, 500, "Internal Server Error");
        transport_.server_control_port = control_receiver_->port();
    }
    if (transport_.server_timing_port == 0) {
        if (!timing_receiver_) timing_receiver_ = std::make_unique<RtpReceiver>();
        if (!timing_receiver_->running() && !timing_receiver_->bind(0)) return base_response(request, 500, "Internal Server Error");
        transport_.server_timing_port = timing_receiver_->port();
    }

    std::ostringstream description;
    description << "RTP SETUP: server_data_port=" << transport_.server_data_port
                << " server_control_port=" << transport_.server_control_port
                << " server_timing_port=" << transport_.server_timing_port;
    if (sdp_.has_video()) description << " (video session present)";
    else if (setup_info_complete_) description << " (binary-plist AirPlay stream setup)";
    log(description.str());

    auto response = base_response(request, 200, "OK");
    response.headers["Session"] = "GWL-AIRPLAY-1";
    response.headers["Transport"] =
        "RTP/AVP/UDP;unicast;mode=record;server_port=" + std::to_string(transport_.server_data_port) +
        ";control_port=" + std::to_string(transport_.server_control_port) +
        ";timing_port=" + std::to_string(transport_.server_timing_port);
    response.headers["Content-Length"] = "0";
    return response;
}

RtspResponse RtspSession::record(const RtspRequest& request) {
    if (!configured_ || !media_receiver_ || !media_receiver_->running()) {
        return base_response(request, 455, "Method Not Valid in This State");
    }
    recording_ = true;
    log("RTSP RECORD: media receiver active");
    if (sdp_.has_video()) log("Screen Mirroring: waiting for video RTP packets");
    auto response = base_response(request, 200, "OK");
    response.headers["Session"] = "GWL-AIRPLAY-1";
    response.headers["Content-Length"] = "0";
    return response;
}

RtspResponse RtspSession::pause(const RtspRequest& request) {
    recording_ = false;
    if (alac_audio_pipeline_) alac_audio_pipeline_->reset();
    auto response = base_response(request, 200, "OK");
    response.headers["Session"] = "GWL-AIRPLAY-1";
    response.headers["Content-Length"] = "0";
    return response;
}

RtspResponse RtspSession::flush(const RtspRequest& request) {
    jitter_buffer_.reset();
    auto response = base_response(request, 200, "OK");
    response.headers["Session"] = "GWL-AIRPLAY-1";
    response.headers["Content-Length"] = "0";
    return response;
}

RtspResponse RtspSession::get_parameter(const RtspRequest& request) {
    auto response = base_response(request, 200, "OK");
    response.headers["Session"] = "GWL-AIRPLAY-1";
    response.headers["Content-Length"] = "0";
    return response;
}

RtspResponse RtspSession::set_parameter(const RtspRequest& request) {
    auto response = base_response(request, 200, "OK");
    response.headers["Session"] = "GWL-AIRPLAY-1";
    response.headers["Content-Length"] = "0";
    return response;
}

RtspResponse RtspSession::teardown(const RtspRequest& request) {
    reset();
    auto response = base_response(request, 200, "OK");
    response.headers["Session"] = "GWL-AIRPLAY-1";
    response.headers["Content-Length"] = "0";
    return response;
}

void RtspSession::set_media_packet_handler(MediaPacketHandler handler) {
    media_packet_handler_ = std::move(handler);
}

void RtspSession::clear_media_packet_handler() {
    media_packet_handler_ = {};
}

void RtspSession::set_alac_audio_pipeline(std::unique_ptr<AlacAudioPipeline> pipeline) {
    alac_audio_pipeline_ = std::move(pipeline);
    if (alac_audio_pipeline_ && configured_ && sdp_.has_audio() && !sdp_.fmtp.empty()) {
        AlacConfig config;
        if (parse_alac_fmtp(sdp_.fmtp, config) && config.valid()) {
            alac_audio_pipeline_->configure(config);
        }
    }
}

void RtspSession::handle_media_packet(const RtpPacket& packet) {
    if (!recording_) return;
    if (!jitter_buffer_.push(packet)) return;

    ++received_packets_;
    received_bytes_ += packet.payload.size();

    if (received_packets_ == 1 || (received_packets_ % 500) == 0) {
        std::ostringstream message;
        message << "RTP media: packets=" << received_packets_
                << " payload_bytes=" << received_bytes_
                << " payload_type=" << static_cast<int>(packet.payload_type)
                << " seq=" << packet.sequence;
        log(message.str());
    }

    while (auto ordered = jitter_buffer_.pop()) {
        if (alac_audio_pipeline_) alac_audio_pipeline_->push(*ordered);
        if (media_packet_handler_) media_packet_handler_(*ordered);
    }
}

} // namespace gwl::airplay2
