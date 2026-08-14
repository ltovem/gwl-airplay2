#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>

namespace gwl::airplay2 {

struct HttpRequest {
    std::string method;
    std::string target;
    std::string protocol = "HTTP/1.1";
    std::map<std::string, std::string> headers;
    std::string body;
};

struct HttpResponse {
    int status = 200;
    std::string reason = "OK";
    std::string protocol = "HTTP/1.1";
    std::string content_type = "text/plain";
    std::map<std::string, std::string> headers;
    std::string body;
};

using HttpHandler = std::function<HttpResponse(const HttpRequest&)>;

class HttpServer {
public:
    HttpServer();
    ~HttpServer();

    bool start(std::uint16_t port, HttpHandler handler);
    void stop();
    bool running() const noexcept;

private:
    class Impl;
    Impl* impl_;
};

} // namespace gwl::airplay2
