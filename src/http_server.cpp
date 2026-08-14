#include "airplay2/http_server.h"

#include <atomic>
#include <cstring>
#include <thread>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using socket_t = SOCKET;
static constexpr socket_t invalid_socket = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using socket_t = int;
static constexpr socket_t invalid_socket = -1;
#endif

namespace gwl::airplay2 {

namespace {
#ifdef _WIN32
void close_socket(socket_t s) { if (s != INVALID_SOCKET) closesocket(s); }
#else
void close_socket(socket_t s) { if (s >= 0) ::close(s); }
#endif
}

class HttpServer::Impl {
public:
    std::atomic<bool> running{false};
    socket_t listen_socket = invalid_socket;
    std::thread thread;
    HttpHandler handler;

    void serve() {
        while (running.load()) {
            sockaddr_in addr{};
#ifdef _WIN32
            int len = sizeof(addr);
#else
            socklen_t len = sizeof(addr);
#endif
            socket_t client = ::accept(listen_socket, reinterpret_cast<sockaddr*>(&addr), &len);
            if (client == invalid_socket) continue;

            char buffer[8192]{};
#ifdef _WIN32
            int n = ::recv(client, buffer, sizeof(buffer) - 1, 0);
#else
            int n = static_cast<int>(::recv(client, buffer, sizeof(buffer) - 1, 0));
#endif
            if (n > 0) {
                std::string raw(buffer, static_cast<size_t>(n));
                auto line_end = raw.find("\r\n");
                auto line = raw.substr(0, line_end == std::string::npos ? raw.size() : line_end);
                auto a = line.find(' ');
                auto b = line.find(' ', a + 1);
                HttpRequest request;
                if (a != std::string::npos && b != std::string::npos) {
                    request.method = line.substr(0, a);
                    request.target = line.substr(a + 1, b - a - 1);
                }
                auto body_pos = raw.find("\r\n\r\n");
                if (body_pos != std::string::npos) request.body = raw.substr(body_pos + 4);

                HttpResponse response = handler ? handler(request) : HttpResponse{};
                const char* reason = response.status == 200 ? "OK" : "Error";
                std::string out = "HTTP/1.1 " + std::to_string(response.status) + " " + reason + "\r\n";
                out += "Content-Type: " + response.content_type + "\r\n";
                out += "Content-Length: " + std::to_string(response.body.size()) + "\r\n";
                out += "Connection: close\r\n\r\n";
                out += response.body;
                ::send(client, out.data(), static_cast<int>(out.size()), 0);
            }
            close_socket(client);
        }
    }
};

HttpServer::HttpServer() : impl_(new Impl) {}
HttpServer::~HttpServer() { stop(); delete impl_; }

bool HttpServer::start(std::uint16_t port, HttpHandler handler) {
    if (impl_->running.exchange(true)) return false;
#ifdef _WIN32
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) { impl_->running = false; return false; }
#endif
    impl_->handler = std::move(handler);
    impl_->listen_socket = ::socket(AF_INET, SOCK_STREAM, 0);
    if (impl_->listen_socket == invalid_socket) { impl_->running = false; return false; }

    int yes = 1;
    setsockopt(impl_->listen_socket, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&yes), sizeof(yes));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    if (::bind(impl_->listen_socket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0 ||
        ::listen(impl_->listen_socket, 16) < 0) {
        close_socket(impl_->listen_socket);
        impl_->listen_socket = invalid_socket;
        impl_->running = false;
#ifdef _WIN32
        WSACleanup();
#endif
        return false;
    }
    impl_->thread = std::thread([this] { impl_->serve(); });
    return true;
}

void HttpServer::stop() {
    if (!impl_ || !impl_->running.exchange(false)) return;
    close_socket(impl_->listen_socket);
    impl_->listen_socket = invalid_socket;
    if (impl_->thread.joinable()) impl_->thread.join();
#ifdef _WIN32
    WSACleanup();
#endif
}

bool HttpServer::running() const noexcept { return impl_->running.load(); }

} // namespace gwl::airplay2
