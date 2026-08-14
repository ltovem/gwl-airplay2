#include "airplay2/airplay_receiver.h"
#include "airplay2/http_server.h"
#include "airplay2/mdns.h"

#include <functional>
#include <iomanip>
#include <random>
#include <sstream>

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

} // namespace

class AirPlayReceiver::Impl {
public:
    HttpServer server;
    MdnsService mdns;
    bool running = false;
    ReceiverConfig config;

    HttpResponse handle(const HttpRequest& request) {
        if (request.target == "/info" || request.target == "/info/") {
            std::ostringstream json;
            json << "{\"name\":\"" << config.device_name
                 << "\",\"model\":\"GWL-AirPlay2\""
                 << ",\"deviceID\":\"" << config.device_id << "\""
                 << ",\"protocols\":[\"airplay\"]"
                 << ",\"audio\":" << (config.enable_audio ? "true" : "false")
                 << ",\"video\":" << (config.enable_video ? "true" : "false")
                 << "}";
            return {200, "application/json", json.str()};
        }
        if (request.method == "OPTIONS") {
            return {200, "text/plain", ""};
        }
        return {404, "text/plain", "Not Found"};
    }
};

AirPlayReceiver::AirPlayReceiver() : impl_(std::make_unique<Impl>()) {}
AirPlayReceiver::~AirPlayReceiver() { stop(); }

bool AirPlayReceiver::start(const ReceiverConfig& config) {
    if (impl_->running) return false;

    impl_->config = config;
    if (impl_->config.device_id.empty()) impl_->config.device_id = make_device_id();

    if (!impl_->server.start(config.port, [this](const HttpRequest& request) {
            return impl_->handle(request);
        })) return false;

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
        impl_->server.stop();
        return false;
    }

    impl_->running = true;
    return true;
}

void AirPlayReceiver::stop() {
    if (!impl_) return;
    impl_->mdns.unpublish();
    impl_->server.stop();
    impl_->running = false;
}

bool AirPlayReceiver::running() const noexcept { return impl_ && impl_->running; }

} // namespace gwl::airplay2
