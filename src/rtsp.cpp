#include "airplay2/rtsp.h"

#include <algorithm>
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

} // namespace

RtspSession::RtspSession() = default;
RtspSession::~RtspSession() = default;

void RtspSession::reset() {
    recording_ = false;
    configured_ = false;
    transport_ = {};
    if (media_receiver_) media_receiver_->close();
    media_receiver_.reset();
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
    if (request.body.empty()) return base_response(request, 400, "Bad Request");
    configured_ = true;
    auto response = base_response(request, 200, "OK");
    response.headers["Content-Length"] = "0";
    return response;
}

RtspResponse RtspSession::setup(const RtspRequest& request) {
    if (!configured_) return base_response(request, 455, "Method Not Valid in This State");
    const auto transport = header_value(request, "Transport");
    transport_.client_control_port = parse_port(transport, "control_port=");
    transport_.client_timing_port = parse_port(transport, "timing_port=");

    if (!media_receiver_) media_receiver_ = std::make_unique<RtpReceiver>();
    if (!media_receiver_->running() && !media_receiver_->bind(0)) {
        return base_response(request, 500, "Internal Server Error");
    }

    transport_.server_data_port = media_receiver_->port();
    transport_.server_control_port = 0;
    transport_.server_timing_port = 0;

    auto response = base_response(request, 200, "OK");
    response.headers["Session"] = "GWL-AIRPLAY-1";
    response.headers["Transport"] =
        "RTP/AVP/UDP;unicast;mode=record;server_port=" + std::to_string(transport_.server_data_port);
    response.headers["Content-Length"] = "0";
    return response;
}

RtspResponse RtspSession::record(const RtspRequest& request) {
    if (!configured_ || !media_receiver_ || !media_receiver_->running()) {
        return base_response(request, 455, "Method Not Valid in This State");
    }
    recording_ = true;
    auto response = base_response(request, 200, "OK");
    response.headers["Session"] = "GWL-AIRPLAY-1";
    response.headers["Content-Length"] = "0";
    return response;
}

RtspResponse RtspSession::pause(const RtspRequest& request) {
    recording_ = false;
    auto response = base_response(request, 200, "OK");
    response.headers["Session"] = "GWL-AIRPLAY-1";
    response.headers["Content-Length"] = "0";
    return response;
}

RtspResponse RtspSession::flush(const RtspRequest& request) {
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

} // namespace gwl::airplay2
