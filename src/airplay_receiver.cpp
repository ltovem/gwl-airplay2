#include "airplay2/airplay_receiver.h"
#include "airplay2/airplay_pairing.h"
#include "airplay2/airplay_hkp_pairing.h"
#include "airplay2/apple_audio_sink.h"
#include "airplay2/http_server.h"
#include "airplay2/mdns.h"
#include "airplay2/rtsp.h"

#include <algorithm>
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
std::vector<std::uint8_t> hex_bytes(const char* text) {
    std::vector<std::uint8_t> out;
    for (const char* p = text; *p;) {
        while (*p == ' ' || *p == '\n' || *p == '\t' || *p == '\r') ++p;
        if (!p[0] || !p[1]) break;
        unsigned int v = 0; std::stringstream ss; ss << std::hex << p[0] << p[1]; ss >> v;
        out.push_back(static_cast<std::uint8_t>(v)); p += 2;
    }
    return out;
}

const std::vector<std::uint8_t>& fairplay_v3_phase1_response() {
    static const auto response = hex_bytes(
        "46504c59030102000000008202039001e1727e0f57f9f5880db104a6257a23f5"
        "cfff1abbe1e93045251afb97eb9fc0011ebe0f3a81df5b691d76acb2f7a5c708"
        "e3d328f56bb39dbde5f29c8a17f481487e3ae863c678325422e6f78e166d18aa"
        "7fd636258bce28726f661f738893ce44311e4be6c0535193e5ef72e868623372"
        "9c227d820c999445d89246c8c359");
    return response;
}
std::vector<std::uint8_t> handle_fairplay_setup(const std::vector<std::uint8_t>& body,
                                                std::vector<std::uint8_t>& key_message,
                                                std::string& error) {
    error.clear();
    if (body.size() < 16 || body[0] != 'F' || body[1] != 'P' || body[2] != 'L' || body[3] != 'Y') { error = "invalid FPLY header"; return {}; }
    if (body[4] != 3 || body[5] != 1) { error = "unsupported FairPlay version"; return {}; }
    const auto phase = body[6];
    if (phase == 1) { if (body.size() != 16) { error = "invalid FairPlay phase-1 length"; return {}; } return fairplay_v3_phase1_response(); }
    if (phase == 3) {
        if (body.size() != 164) { error = "invalid FairPlay phase-3 length"; return {}; }
        key_message = body;
        static const std::uint8_t header[12] = {0x46,0x50,0x4c,0x59,0x03,0x01,0x04,0x00,0x00,0x00,0x00,0x14};
        std::vector<std::uint8_t> response(header, header + 12);
        response.insert(response.end(), body.begin() + 144, body.begin() + 164);
        return response;
    }
    error = "unsupported FairPlay phase"; return {};
}

std::string initial_volume_binary_plist() {
    static const unsigned char bytes[] = {
        0x62,0x70,0x6c,0x69,0x73,0x74,0x30,0x30,0xd1,0x01,0x02,0x5d,0x69,0x6e,0x69,
        0x74,0x69,0x61,0x6c,0x56,0x6f,0x6c,0x75,0x6d,0x65,0x13,0xff,0xff,0xff,0xff,0xff,
        0xff,0x88,0x08,0x0b,0x19,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x01,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00,0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0x00,0x00,0x22
    };
    return std::string(reinterpret_cast<const char*>(bytes), sizeof(bytes));
}

std::string device_info_binary_plist(const std::string& device_id) {
    // Valid binary plist generated from the AirPlay /info schema. The old
    // hand-written plist advertised an unrelated feature integer, which made
    // recent Apple senders drop the connection immediately after /info.
    static const char* hex =
        "62706c6973743030dc0102030405060708090a0b0c0d0e0f0f10111213141516175864657669636549445866656174757265735f10116b656570416c6976654c6f77506f7765725f10186b656570416c69766553656e6453746174734173426f64795c6d616e756661637475726572556d6f64656c546e616d655270695f100f70726f746f636f6c56657273696f6e5d736f7572636556657273696f6e5b737461747573466c6167735276765f101130303a30303a30303a30303a30303a3030130000001e5a7ffff7095347574c5a4170706c655456332c325f101047574c20416972506c61792044656d6f5f102430303030303030302d303030302d343030302d383030302d30303030303030303030303053312e31563232302e36381044100200080021002a003300470062006f0075007a007d008f009d00a900ac00c000c900ca00ce00d900ec01130117011e01200000000000000201000000000000001800000000000000000000000000000122";
    auto bytes = hex_bytes(hex);
    const std::string device_placeholder = "00:00:00:00:00:00";
    const std::string pi_placeholder = "00000000-0000-4000-8000-000000000000";
    const std::string pi = make_pi(device_id);
    auto replace = [&bytes](const std::string& from, const std::string& to) {
        if (from.size() != to.size()) return false;
        const auto it = std::search(bytes.begin(), bytes.end(), from.begin(), from.end());
        if (it == bytes.end()) return false;
        std::copy(to.begin(), to.end(), it);
        return true;
    };
    replace(device_placeholder, device_id);
    replace(pi_placeholder, pi);
    return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}
}

class AirPlayReceiver::Impl {
public:
    HttpServer server; MdnsService mdns; bool running = false; ReceiverConfig config; std::string protocol_identity;
    void log(const std::string& message) const { if (config.log_callback) config.log_callback(message); }

    HttpResponse handle_request(const HttpRequest& request, RtspSession& rtsp,
                                AirPlayTransientPairing& transient,
                                AirPlayHkpPairing& hkp,
                                std::vector<std::uint8_t>& fairplay_key_message) {
        log(request.method + " " + request.target);
        if (request.target == "/info" || request.target == "/info/") {
            if (request.method != "GET") return response_for(405, "Method Not Allowed", request);
            if (request.body.empty()) {
                const std::string body = initial_volume_binary_plist();
                HttpResponse r; r.status = 200; r.reason = "OK"; r.protocol = request.protocol.empty() ? "RTSP/1.0" : request.protocol;
                r.headers["Content-Type"] = "application/x-apple-binary-plist";
                r.headers["Content-Length"] = std::to_string(body.size()); r.body = body;
                log("GET /info -> 200 (initialVolume binary plist)");
                return r;
            }
            const std::string body = device_info_binary_plist(config.device_id);
            log("GET /info -> 200 (device info binary plist, " + std::to_string(body.size()) + " bytes)");
            HttpResponse r; r.status = 200; r.reason = "OK"; r.protocol = request.protocol.empty() ? "RTSP/1.0" : request.protocol;
            r.headers["Content-Type"] = "application/x-apple-binary-plist";
            r.headers["Content-Length"] = std::to_string(body.size()); r.body = body; return r;
        }
        if (request.target == "/pair-setup") {
            if (request.method != "POST") return response_for(405, "Method Not Allowed", request);
            std::vector<std::uint8_t> body(request.body.begin(), request.body.end()); std::vector<std::uint8_t> response; std::string error;
            if (body.size() == 32) {
                if (!hkp.handle_pair_setup(body, response, error)) { log("[Pairing] HKP /pair-setup failed: " + error); return response_for(400, "Bad Request", request); }
                HttpResponse r; r.status = 200; r.reason = "OK"; r.protocol = "RTSP/1.0"; r.headers["Content-Type"] = "application/octet-stream"; r.headers["Content-Length"] = std::to_string(response.size()); r.body.assign(reinterpret_cast<const char*>(response.data()), response.size()); log("[Pairing] HKP /pair-setup -> 200 (Ed25519 public key, 32 bytes)"); return r;
            }
            if (!transient.handle(body, response, error)) { log("[Pairing] /pair-setup failed: " + error); return response_for(400, "Bad Request", request); }
            HttpResponse r; r.status = 200; r.reason = "OK"; r.protocol = "RTSP/1.0"; r.headers["Content-Type"] = "application/octet-stream"; r.headers["Content-Length"] = std::to_string(response.size()); r.body.assign(reinterpret_cast<const char*>(response.data()), response.size()); if (transient.complete()) log("[Pairing] transient Pair-Setup M4 complete; SRP shared secret established"); return r;
        }
        if (request.target == "/pair-verify") {
            if (request.method != "POST") return response_for(405, "Method Not Allowed", request);
            std::vector<std::uint8_t> body(request.body.begin(), request.body.end()); std::vector<std::uint8_t> response; std::string error;
            if (!hkp.handle_pair_verify(body, response, error)) { log("[Pairing] HKP /pair-verify failed: " + error); return response_for(400, "Bad Request", request); }
            HttpResponse r; r.status = 200; r.reason = "OK"; r.protocol = "RTSP/1.0"; r.headers["Content-Type"] = "application/octet-stream"; r.headers["Content-Length"] = std::to_string(response.size()); r.body.assign(reinterpret_cast<const char*>(response.data()), response.size()); if (hkp.verified()) log("[Pairing] HKP /pair-verify verified"); return r;
        }
        if (request.target == "/fp-setup") {
            if (request.method != "POST") return response_for(405, "Method Not Allowed", request);
            const std::vector<std::uint8_t> body(request.body.begin(), request.body.end()); std::string error; const auto response = handle_fairplay_setup(body, fairplay_key_message, error);
            if (response.empty()) { log("[FairPlay] /fp-setup failed: " + error); return response_for(400, "Bad Request", request); }
            HttpResponse r; r.status = 200; r.reason = "OK"; r.protocol = "RTSP/1.0"; r.headers["Content-Type"] = "application/octet-stream"; r.headers["Content-Length"] = std::to_string(response.size()); r.body.assign(reinterpret_cast<const char*>(response.data()), response.size()); if (body[6] == 1) log("[FairPlay] v3 /fp-setup phase 1 -> 200 (142 bytes)"); else log("[FairPlay] v3 /fp-setup phase 3 -> 200 (32 bytes; key message stored)"); return r;
        }
        if (is_rtsp_media_request(request)) {
            RtspRequest rr; rr.method = request.method; rr.uri = request.target; rr.body = request.body; rr.headers = request.headers;
            const auto cseq = request.headers.find("CSeq"); if (cseq != request.headers.end()) { try { rr.cseq = std::stoi(cseq->second); } catch (...) { rr.cseq = 0; } }
            const auto rs = rtsp.handle(rr); log("RTSP " + std::to_string(rs.status) + " " + rs.reason);
            HttpResponse r; r.status = rs.status; r.reason = rs.reason; r.protocol = "RTSP/1.0"; r.content_type.clear(); r.headers = rs.headers; r.body = rs.body; return r;
        }
        return response_for(404, "Not Found", request);
    }
};

AirPlayReceiver::AirPlayReceiver() : impl_(std::make_unique<Impl>()) {}
AirPlayReceiver::~AirPlayReceiver() { stop(); }

bool AirPlayReceiver::start(const ReceiverConfig& config) {
    if (impl_->running) return false; impl_->config = config; if (impl_->config.device_id.empty()) impl_->config.device_id = make_device_id(); impl_->protocol_identity = make_pi(impl_->config.device_id); impl_->log("Starting receiver '" + impl_->config.device_name + "'"); impl_->log("HTTP/RTSP port: " + std::to_string(impl_->config.port));
    const auto factory = [this]() -> HttpHandler {
        auto session = std::make_shared<RtspSession>(); auto transient = std::make_shared<AirPlayTransientPairing>(); auto hkp = std::make_shared<AirPlayHkpPairing>(); auto fairplay_key_message = std::make_shared<std::vector<std::uint8_t>>();
        session->set_log_handler([this](const std::string& message) { impl_->log(message); });
        if (impl_->config.audio_sink_factory) { auto sink = impl_->config.audio_sink_factory(); if (sink) session->set_alac_audio_pipeline(std::make_unique<AlacAudioPipeline>(create_software_alac_decoder(), std::move(sink))); }
        return [this, session = std::move(session), transient = std::move(transient), hkp = std::move(hkp), fairplay_key_message = std::move(fairplay_key_message)](const HttpRequest& request) { return impl_->handle_request(request, *session, *transient, *hkp, *fairplay_key_message); };
    };
    if (!impl_->server.start_per_connection(config.port, factory)) { impl_->log("ERROR: failed to bind HTTP/RTSP server"); return false; }
    const std::vector<MdnsTxtRecord> records = {{"deviceid", impl_->config.device_id}, {"model", "AppleTV3,2"}, {"srcvers", "220.68"}, {"protovers", "1.1"}, {"features", "0x5A7FFFF7,0x1E"}, {"flags", "0x44"}, {"vv", "2"}, {"pi", impl_->protocol_identity}, {"pw", "false"}};
    if (!impl_->mdns.publish(impl_->config.device_name, impl_->config.port, records)) { impl_->log("ERROR: failed to publish _airplay._tcp via mDNS"); impl_->server.stop(); return false; }
    impl_->running = true; impl_->log("Published _airplay._tcp"); impl_->log("Device ID: " + impl_->config.device_id); impl_->log("AirPlay model: AppleTV3,2"); impl_->log("AirPlay features: 0x5A7FFFF7,0x1E (video + screen mirroring + audio)"); impl_->log("Waiting for AirPlay connection..."); return true;
}
void AirPlayReceiver::stop() { if (!impl_) return; if (impl_->running) impl_->log("Stopping receiver"); impl_->mdns.unpublish(); impl_->server.stop(); impl_->running = false; }
bool AirPlayReceiver::running() const noexcept { return impl_ && impl_->running; }
} // namespace gwl::airplay2
