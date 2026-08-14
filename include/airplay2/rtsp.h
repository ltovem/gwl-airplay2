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

class RtspSession {
public:
    RtspResponse handle(const RtspRequest& request);

    bool configured() const noexcept { return configured_; }
    bool recording() const noexcept { return recording_; }

private:
    RtspResponse options(const RtspRequest& request);
    RtspResponse announce(const RtspRequest& request);
    RtspResponse setup(const RtspRequest& request);
    RtspResponse record(const RtspRequest& request);
    RtspResponse flush(const RtspRequest& request);
    RtspResponse teardown(const RtspRequest& request);

    bool configured_ = false;
    bool recording_ = false;
};

} // namespace gwl::airplay2
