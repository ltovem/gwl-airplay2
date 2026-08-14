#include "airplay2/airplay_receiver.h"
#include "airplay2/http_server.h"

#include <sstream>
#include <utility>

namespace gwl::airplay2 {

class AirPlayReceiver::Impl {
public:
    HttpServer server;
    bool running = false;
    ReceiverConfig config;

    HttpResponse handle(const HttpRequest& request) {
        if (request.target == "/info" || request.target == "/info/" ) {
            std::ostringstream json;
            json << "{\"name\":\"" << config.device_name
                 << "\",\"model\":\"GWL-AirPlay2\""
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
    if (!impl_->server.start(config.port, [this](const HttpRequest& request) {
            return impl_->handle(request);
        })) return false;
    impl_->running = true;
    return true;
}

void AirPlayReceiver::stop() {
    if (!impl_) return;
    impl_->server.stop();
    impl_->running = false;
}

bool AirPlayReceiver::running() const noexcept { return impl_ && impl_->running; }

} // namespace gwl::airplay2
