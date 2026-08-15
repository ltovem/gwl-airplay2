#include "airplay2/airplay_receiver.h"
#include "airplay2/apple_audio_sink.h"
#include "airplay2/http_server.h"
#include "airplay2/mdns.h"
#include "airplay2/rtsp.h"

#include <chrono>
#include <csignal>
#include <iomanip>
#include <memory>
#include <random>
#include <sstream>
#include <thread>

namespace gwl::airplay2 {
namespace {

std::string make_device_id() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> byte(0, 255);
    std::ostringstream out;
    for (int i = 0; i < 6; ++i) {
        if (i) out << ':';
        out << std::uppercase << std::hex << std::setw(2) << std::setfill('0') << byte(gen);
    }
    return out.str();
}

bool is_rtsp_request(const HttpRequest& request) {
    return request.protocol == "RTSP/1.0" || request.method == "ANNOUNCE" ||
           request.method == "SETUP" || request.method == "RECORD" ||
           request.method == "FLUSH" || request.method == "TEARDOWN" ||
           request.method == "GET_PARAMETER" || request.method == "SET_PARAMETER" ||
           request.method == "PAUSE";
}

} // namespace

class AirPlayReceiver::Impl {
public:
    HttpServer server;
    MdnsService mdns;
    bool running = false;
    ReceiverConfig config;

    void log(const std::string& message) const {
        if (config.log_callback) config.log_callback(message);
    }

    HttpResponse handle_request(const HttpRequest& request, RtspSession& rtsp) {
        std::ostringstream line;
        line << request.method << " " << request.target;
        log(line.str());

        if (is_rtsp_request(request)) {
            RtspRequest rtsp_request;
            rtsp_request.method = request.method;
            rtsp_request.uri = request.target;
            rtsp_request.body = request.body;
            rtsp_request.headers = request.headers;
            const auto cseq = request.headers.find("CSeq");
            if (cseq != request.headers.end()) {
                try { rtsp_request.cseq = std::stoi(cseq->second); } catch (...) { rtsp_request.cseq = 0; }
            }

            const RtspResponse rtsp_response = rtsp.handle(rtsp_request);
            std::ostringstream result;
            result << "RTSP " << rtsp_response.status << " " << rtsp_response.reason;
            log(result.str());

            HttpResponse response;
            response.status = rtsp_response.status;
            response.reason = rtsp_response.reason;
            response.protocol = "RTSP/1.0";
            response.content_type.clear();
            response.headers = rtsp_response.headers;
            response.body = rtsp_response.body;
            return response;
        }

        if (request.target == "/info" || request.target == "/info/") {
            std::ostringstream json;
            json << "{\"name\":\"" << config.device_name
                 << "\",\"model\":\"GWL-AirPlay2\""
                 << ",\"deviceID\":\"" << config.device_id << "\""
                 << ",\"protocols\":[\"airplay\"]"
                 << ",\"audio\":" << (config.enable_audio ? "true" : "false")
                 << ",\"video\":" << (config.enable_video ? "true" : "false")
                 << "}";
            log("GET /info -> 200");
            return {200, "OK", "HTTP/1.1", "application/json", {}, json.str()};
        }

        if (request.method == "OPTIONS") return {200, "OK", "HTTP/1.1", "text/plain", {}, ""};
        return {404, "Not Found", "HTTP/1.1", "text/plain", {}, "Not Found"};
    }
};

AirPlayReceiver::AirPlayReceiver() : impl_(std::make_unique<Impl>()) {}
AirPlayReceiver::~AirPlayReceiver() { stop(); }

bool AirPlayReceiver::start(const ReceiverConfig& config) {
    if (impl_->running) return false;

    impl_->config = config;
    if (impl_->config.device_id.empty()) impl_->config.device_id = make_device_id();
    impl_->log("Starting receiver '" + impl_->config.device_name + "'");
    impl_->log("HTTP/RTSP port: " + std::to_string(impl_->config.port));

    const auto factory = [this]() -> HttpHandler {
        auto session = std::make_shared<RtspSession>();
        if (impl_->config.audio_sink_factory) {
            auto sink = impl_->config.audio_sink_factory();
            if (sink) {
                session->set_alac_audio_pipeline(
                    std::make_unique<AlacAudioPipeline>(
                        create_software_alac_decoder(), std::move(sink)));
            }
        }
        return [this, session = std::move(session)](const HttpRequest& request) {
            return impl_->handle_request(request, *session);
        };
    };

    if (!impl_->server.start_per_connection(config.port, factory)) {
        impl_->log("ERROR: failed to bind HTTP/RTSP server");
        return false;
    }

    const std::vector<MdnsTxtRecord> records = {
        {"deviceid", impl_->config.device_id},
        {"model", "GWL-AirPlay2"},
        {"srcvers", "1.0"},
        {"protovers", "1.1"},
        {"features", "0x5A7FFFF7,0x1E"},
        {"flags", "0x4"},
        {"vv", "1"},
        {"pi", "0.8"}
    };

    if (!impl_->mdns.publish(impl_->config.device_name, impl_->config.port, records)) {
        impl_->log("ERROR: failed to publish _airplay._tcp via mDNS");
        impl_->server.stop();
        return false;
    }

    impl_->running = true;
    impl_->log("Published _airplay._tcp");
    impl_->log("Device ID: " + impl_->config.device_id);
    impl_->log("Waiting for AirPlay connection...");
    return true;
}

void AirPlayReceiver::stop() {
    if (!impl_) return;
    if (impl_->running) impl_->log("Stopping receiver");
    impl_->mdns.unpublish();
    impl_->server.stop();
    impl_->running = false;
}

bool AirPlayReceiver::running() const noexcept { return impl_ && impl_->running; }

} // namespace gwl::airplay2
