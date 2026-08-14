#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>

#include "airplay2/crypto_session.h"
#include "airplay2/rtp.h"
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
};

class RtspSession {
public:
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
    RtpReceiver* media_receiver() noexcept { return media_receiver_.get(); }

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

    bool configured_ = false;
    bool recording_ = false;
    RtpTransport transport_{};
    AirPlaySdp sdp_{};
    CryptoSession crypto_{};
    std::unique_ptr<RtpReceiver> media_receiver_;
};

} // namespace gwl::airplay2
