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

bool has_header(const RtspRequest& request, const char* name) {
    return std::any_of(request.headers.begin(), request.headers.end(),
        [name](const auto& item) {
            if (item.first.size() != std::char_traits<char>::length(name)) return false;
            for (std::size_t i = 0; i < item.first.size(); ++i) {
                if (std::tolower(static_cast<unsigned char>(item.first[i])) !=
                    std::tolower(static_cast<unsigned char>(name[i]))) return false;
            }
            return true;
        });
}

} // namespace

RtspResponse RtspSession::handle(const RtspRequest& request) {
    if (request.method == "OPTIONS") return options(request);
    if (request.method == "ANNOUNCE") return announce(request);
    if (request.method == "SETUP") return setup(request);
    if (request.method == "RECORD") return record(request);
    if (request.method == "FLUSH") return flush(request);
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

    auto response = base_response(request, 200, "OK");
    response.headers["Session"] = "GWL-AIRPLAY-1";
    // This is deliberately a protocol skeleton. Transport negotiation and RTP
    // sockets are implemented by the media layer in the next phase.
    response.headers["Transport"] = has_header(request, "Transport")
        ? "RTP/AVP/UDP;unicast;mode=record"
        : "RTP/AVP/UDP;unicast;mode=record";
    response.headers["Content-Length"] = "0";
    return response;
}

RtspResponse RtspSession::record(const RtspRequest& request) {
    if (!configured_) return base_response(request, 455, "Method Not Valid in This State");
    recording_ = true;
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

RtspResponse RtspSession::teardown(const RtspRequest& request) {
    recording_ = false;
    configured_ = false;
    auto response = base_response(request, 200, "OK");
    response.headers["Session"] = "GWL-AIRPLAY-1";
    response.headers["Content-Length"] = "0";
    return response;
}

} // namespace gwl::airplay2
