#include "airplay2/airplay_receiver.h"
#include "airplay2/airplay_pairing.h"
#include "airplay2/airplay_hkp_pairing.h"
#include "airplay2/apple_audio_sink.h"
#include "airplay2/http_server.h"
#include "airplay2/mdns.h"
#include "airplay2/rtsp.h"

#include <iomanip>
#include <memory>
#include <random>
#include <sstream>
#include <vector>

namespace gwl::airplay2 {
namespace {
std::string make_device_id() {
    std::random_device rd; std::mt19937 gen(rd()); std::uniform_int_distribution<int> byte(0, 255);
    std::ostringstream out;
    for (int i = 0; i < 6; ++i) { if (i) out << ':'; out << std::uppercase << std::hex << std::setw(2) << std::setfill('0') << byte(gen); }
    return out.str();
}
std::string make_pi(const std::string& device_id) {
    std::string hex; for (char c : device_id) if (c != ':') hex.push_back(c);
    if (hex.size() < 32) hex.append(32 - hex.size(), '0');
    return hex.substr(0, 8) + "-" + hex.substr(8, 4) + "-4" + hex.substr(13, 3) + "-8" + hex.substr(17, 3) + "-" + hex.substr(20, 12);
}
bool is_rtsp_media_request(const HttpRequest& request) {
    return request.method == "OPTIONS" || request.method == "ANNOUNCE" || request.method == "SETUP" || request.method == "RECORD" || request.method == "FLUSH" || request.method == "TEARDOWN" || request.method == "GET_PARAMETER" || request.method == "SET_PARAMETER" || request.method == "PAUSE";
}
HttpResponse response_for(int status, const char* reason, const HttpRequest& request) {
    HttpResponse r; r.status = status; r.reason = reason; r.protocol = request.protocol.empty() ? "RTSP/1.0" : request.protocol; r.content_type.clear(); return r;
}
}

class AirPlayReceiver::Impl {
public:
    HttpServer server; MdnsService mdns; bool running = false; ReceiverConfig config; std::string protocol_identity;
    void log(const std::string& message) const { if (config.log_callback) config.log_callback(message); }

    HttpResponse handle_request(const HttpRequest& request, RtspSession& rtsp,
                                AirPlayTransientPairing& transient,
                                AirPlayHkpPairing& hkp) {
        log(request.method + " " + request.target);
        // /info and pairing are AirPlay control endpoints even though the client uses RTSP/1.0.
        if (request.target == "/info" || request.target == "/info/") {
            if (request.method != "GET") return response_for(405, "Method Not Allowed", request);
            std::ostringstream json;
            json << "{\"name\":\"" << config.device_name << "\",\"model\":\"AppleTV3,2\",\"deviceID\":\"" << config.device_id
                 << "\",\"protocols\":[\"airplay\"],\"audio\":" << (config.enable_audio ? "true" : "false")
                 << ",\"video\":" << (config.enable_video ? "true" : "false") << ",\"features\":\"0x5A7FFFF7,0x1E\"}";
            const std::string body = json.str();
            log("GET /info -> 200");
            HttpResponse r; r.status = 200; r.reason = "OK"; r.protocol = request.protocol.empty() ? "RTSP/1.0" : request.protocol; r.content_type = "application/json"; r.headers["Content-Type"] = "application/json"; r.headers["Content-Length"] = std::to_string(body.size()); r.body = body; return r;
        }
        if (request.target == "/pair-setup") {
            if (request.method != "POST") return response_for(405, "Method Not Allowed", request);
            std::vector<std::uint8_t> body(request.body.begin(), request.body.end());
            std::vector<std::uint8_t> response;
            std::string error;

            // Current iOS/macOS mirroring clients use the HKP path here: the
            // body is exactly 32 opaque bytes, not HomeKit TLV8/SRP.
            if (body.size() == 32) {
                if (!hkp.handle_pair_setup(body, response, error)) {
                    log("[Pairing] HKP /pair-setup failed: " + error);
                    return response_for(400, "Bad Request", request);
                }
                HttpResponse r; r.status = 200; r.reason = "OK"; r.protocol = "RTSP/1.0";
                r.headers["Content-Type"] = "application/octet-stream";
                r.headers["Content-Length"] = std::to_string(response.size());
                r.body.assign(reinterpret_cast<const char*>(response.data()), response.size());
                log("[Pairing] HKP /pair-setup -> 200 (Ed25519 public key, 32 bytes)");
                return r;
            }

            // Keep the HomeKit transient pairing implementation for receivers
            // that negotiate the TLV8/SRP variant.
            if (!transient.handle(body, response, error)) {
                log("[Pairing] /pair-setup failed: " + error);
                return response_for(400, "Bad Request", request);
            }
            HttpResponse r; r.status = 200; r.reason = "OK"; r.protocol = "RTSP/1.0";
            r.headers["Content-Type"] = "application/octet-stream";
            r.headers["Content-Length"] = std::to_string(response.size());
            r.body.assign(reinterpret_cast<const char*>(response.data()), response.size());
            if (transient.complete()) log("[Pairing] transient Pair-Setup M4 complete; SRP shared secret established");
            return r;
        }
        if (request.target == "/pair-verify") {
            if (request.method != "POST") return response_for(405, "Method Not Allowed", request);
            std::vector<std::uint8_t> body(request.body.begin(), request.body.end());
            std::vector<std::uint8_t> response;
            std::string error;
            if (!hkp.handle_pair_verify(body, response, error)) {
                log("[Pairing] HKP /pair-verify failed: " + error);
                return response_for(400, "Bad Request", request);
            }
            HttpResponse r; r.status = 200; r.reason = "OK"; r.protocol = "RTSP/1.0";
            r.headers["Content-Type"] = "application/octet-stream";
            r.headers["Content-Length"] = std::to_string(response.size());
            r.body.assign(reinterpret_cast<const char*>(response.data()), response.size());
            if (hkp.verified()) log("[Pairing] HKP /pair-verify verified");
            return r;
        }
        if (is_rtsp_media_request(request)) {
            RtspRequest rr; rr.method = request.method; rr.uri = request.target; rr.body = request.body; rr.headers = request.headers;
            const auto cseq = request.headers.find("CSeq");
            if (cseq != request.headers.end()) { try { rr.cseq = std::stoi(cseq->second); } catch (...) { rr.cseq = 0; } }
            const auto rs = rtsp.handle(rr); log("RTSP " + std::to_string(rs.status) + " " + rs.reason);
            HttpResponse r; r.status = rs.status; r.reason = rs.reason; r.protocol = "RTSP/1.0"; r.content_type.clear(); r.headers = rs.headers; r.body = rs.body; return r;
        }
        return response_for(404, "Not Found", request);
    }
};

AirPlayReceiver::AirPlayReceiver() : impl_(std::make_unique<Impl>()) {}
AirPlayReceiver::~AirPlayReceiver() { stop(); }

bool AirPlayReceiver::start(const ReceiverConfig& config) {
    if (impl_->running) return false;
    impl_->config = config;
    if (impl_->config.device_id.empty()) impl_->config.device_id = make_device_id();
    impl_->protocol_identity = make_pi(impl_->config.device_id);
    impl_->log("Starting receiver '" + impl_->config.device_name + "'");
    impl_->log("HTTP/RTSP port: " + std::to_string(impl_->config.port));
    const auto factory = [this]() -> HttpHandler {
        auto session = std::make_shared<RtspSession>();
        auto transient = std::make_shared<AirPlayTransientPairing>();
        auto hkp = std::make_shared<AirPlayHkpPairing>();
        session->set_log_handler([this](const std::string& message) { impl_->log(message); });
        if (impl_->config.audio_sink_factory) {
            auto sink = impl_->config.audio_sink_factory();
            if (sink) session->set_alac_audio_pipeline(std::make_unique<AlacAudioPipeline>(create_software_alac_decoder(), std::move(sink)));
        }
        return [this, session = std::move(session), transient = std::move(transient), hkp = std::move(hkp)](const HttpRequest& request) { return impl_->handle_request(request, *session, *transient, *hkp); };
    };
    if (!impl_->server.start_per_connection(config.port, factory)) { impl_->log("ERROR: failed to bind HTTP/RTSP server"); return false; }
    const std::vector<MdnsTxtRecord> records = {{"deviceid", impl_->config.device_id}, {"model", "AppleTV3,2"}, {"srcvers", "220.68"}, {"protovers", "1.1"}, {"features", "0x5A7FFFF7,0x1E"}, {"flags", "0x44"}, {"vv", "2"}, {"pi", impl_->protocol_identity}, {"pw", "false"}};
    if (!impl_->mdns.publish(impl_->config.device_name, impl_->config.port, records)) { impl_->log("ERROR: failed to publish _airplay._tcp via mDNS"); impl_->server.stop(); return false; }
    impl_->running = true;
    impl_->log("Published _airplay._tcp"); impl_->log("Device ID: " + impl_->config.device_id); impl_->log("AirPlay model: AppleTV3,2"); impl_->log("AirPlay features: 0x5A7FFFF7,0x1E (video + screen mirroring + audio)"); impl_->log("Waiting for AirPlay connection..."); return true;
}
void AirPlayReceiver::stop() { if (!impl_) return; if (impl_->running) impl_->log("Stopping receiver"); impl_->mdns.unpublish(); impl_->server.stop(); impl_->running = false; }
bool AirPlayReceiver::running() const noexcept { return impl_ && impl_->running; }
} // namespace gwl::airplay2