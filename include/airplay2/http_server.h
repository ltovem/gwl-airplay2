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
using HttpHandlerFactory = std::function<HttpHandler()>;

class HttpServer {
public:
    HttpServer();
    ~HttpServer();

    bool start(std::uint16_t port, HttpHandler handler);
    bool start_per_connection(std::uint16_t port, HttpHandlerFactory factory);
    void stop();
    bool running() const noexcept;
    std::uint16_t port() const noexcept;

private:
    class Impl;
    Impl* impl_;
};

} // namespace gwl::airplay2
