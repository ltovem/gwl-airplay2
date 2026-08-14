#pragma once

#include <cstdint>
#include <map>
#include <string>

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
};

class RtspSession {
public:
    RtspResponse handle(const RtspRequest& request);

    bool configured() const noexcept { return configured_; }
    bool recording() const noexcept { return recording_; }
    const RtpTransport& transport() const noexcept { return transport_; }

private:
    RtspResponse options(const RtspRequest& request);
    RtspResponse announce(const RtspRequest& request);
    RtspResponse setup(const RtspRequest& request);
    RtspResponse record(const RtspRequest& request);
    RtspResponse flush(const RtspRequest& request);
    RtspResponse teardown(const RtspRequest& request);

    bool configured_ = false;
    bool recording_ = false;
    RtpTransport transport_{};
};

} // namespace gwl::airplay2
