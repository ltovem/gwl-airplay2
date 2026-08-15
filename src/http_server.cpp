#include "airplay2/http_server.h"

#include <array>
#include <atomic>
#include <cctype>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <thread>
#include <utility>
#include <vector>

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

std::string peer_address(const sockaddr_in& addr) {
    char text[INET_ADDRSTRLEN] = {};
#ifdef _WIN32
    inet_ntop(AF_INET, const_cast<IN_ADDR*>(&addr.sin_addr), text, sizeof(text));
#else
    inet_ntop(AF_INET, &addr.sin_addr, text, sizeof(text));
#endif
    std::ostringstream out;
    out << (text[0] ? text : "?") << ':' << ntohs(addr.sin_port);
    return out.str();
}

std::string trim(std::string value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) value.erase(value.begin());
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) value.pop_back();
    return value;
}

std::string header_value(const std::map<std::string, std::string>& headers, const char* name) {
    for (const auto& item : headers) {
        if (item.first.size() != std::strlen(name)) continue;
        bool equal = true;
        for (std::size_t i = 0; i < item.first.size(); ++i) {
            if (std::tolower(static_cast<unsigned char>(item.first[i])) !=
                std::tolower(static_cast<unsigned char>(name[i]))) { equal = false; break; }
        }
        if (equal) return item.second;
    }
    return {};
}

std::string hex_preview(const std::string& data, std::size_t max_bytes = 128) {
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    const std::size_t n = std::min(data.size(), max_bytes);
    for (std::size_t i = 0; i < n; ++i) {
        if (i) out << ' ';
        out << std::setw(2) << static_cast<unsigned int>(static_cast<unsigned char>(data[i]));
    }
    if (data.size() > max_bytes) out << " ...";
    return out.str();
}

bool parse_request(std::string& input, HttpRequest& request) {
    const auto header_end = input.find("\r\n\r\n");
    if (header_end == std::string::npos) return false;
    const auto first_end = input.find("\r\n");
    if (first_end == std::string::npos || first_end > header_end) return false;

    std::istringstream first_stream(input.substr(0, first_end));
    if (!(first_stream >> request.method >> request.target >> request.protocol)) return false;

    request.headers.clear();
    std::size_t line_start = first_end + 2;
    while (line_start < header_end) {
        const auto line_end = input.find("\r\n", line_start);
        if (line_end == std::string::npos || line_end > header_end) return false;
        const auto colon = input.find(':', line_start);
        if (colon == std::string::npos || colon > line_end) return false;
        request.headers[trim(input.substr(line_start, colon - line_start))] =
            trim(input.substr(colon + 1, line_end - colon - 1));
        line_start = line_end + 2;
    }

    std::size_t content_length = 0;
    const auto length = header_value(request.headers, "Content-Length");
    if (!length.empty()) {
        try { content_length = std::stoul(length); }
        catch (...) { return false; }
    }
    const std::size_t total = header_end + 4 + content_length;
    if (input.size() < total) return false;
    request.body.assign(input.data() + header_end + 4, content_length);
    input.erase(0, total);
    return true;
}

} // namespace

class HttpServer::Impl {
public:
    std::atomic<bool> running{false};
    socket_t listen_socket = invalid_socket;
    std::uint16_t bound_port = 0;
    std::thread thread;
    HttpHandlerFactory factory;

    // A per-connection handler may own the AirPlay RTSP session.  AirPlay 2
    // can close the main RTSP TCP connection after SETUP while keeping the
    // event channel alive.  Keep the handler/session alive for the lifetime
    // of this server instead of destroying it at EOF.
    std::vector<HttpHandler> retained_handlers;

    void serve_client(socket_t client, const std::string& peer) {
        std::cerr << "[HTTP] connection accepted from " << peer << std::endl;
        HttpHandler handler = factory ? factory() : HttpHandler{};
        if (handler) {
            retained_handlers.push_back(handler);
            std::cerr << "[HTTP] per-connection handler retained for " << peer
                      << " (AirPlay session lifetime)" << std::endl;
        }
        std::string input;
        input.reserve(16384);
        std::array<char, 8192> buffer{};

        while (running.load()) {
#ifdef _WIN32
            const int n = ::recv(client, buffer.data(), static_cast<int>(buffer.size()), 0);
#else
            const int n = static_cast<int>(::recv(client, buffer.data(), buffer.size(), 0));
#endif
            if (n <= 0) {
                std::cerr << "[HTTP] connection closed from " << peer << " (recv=" << n << ")" << std::endl;
                break;
            }
            input.append(buffer.data(), static_cast<std::size_t>(n));
            std::cerr << "[HTTP] received " << n << " bytes from " << peer << std::endl;

            while (true) {
                HttpRequest request;
                if (!parse_request(input, request)) break;
                std::cerr << "[HTTP] request from " << peer << ": "
                          << request.method << " " << request.target << " " << request.protocol
                          << std::endl;
                if (!request.body.empty()) {
                    std::cerr << "[HTTP] body length=" << request.body.size()
                              << " hex=" << hex_preview(request.body) << std::endl;
                }
                HttpResponse response = handler ? handler(request) : HttpResponse{};
                if (response.reason.empty()) response.reason = response.status == 200 ? "OK" : "Error";
                if (response.protocol.empty()) response.protocol = request.protocol.empty() ? "HTTP/1.1" : request.protocol;

                std::string out = response.protocol + " " + std::to_string(response.status) + " " + response.reason + "\r\n";
                if (!response.content_type.empty()) out += "Content-Type: " + response.content_type + "\r\n";
                out += "Content-Length: " + std::to_string(response.body.size()) + "\r\n";
                for (const auto& header : response.headers) out += header.first + ": " + header.second + "\r\n";
                out += "\r\n";
                out += response.body;
#ifdef _WIN32
                if (::send(client, out.data(), static_cast<int>(out.size()), 0) <= 0) return;
#else
                if (::send(client, out.data(), out.size(), 0) <= 0) return;
#endif
                std::cerr << "[HTTP] response to " << peer << ": "
                          << response.status << " " << response.reason << std::endl;

                const auto connection = header_value(request.headers, "Connection");
                if (connection == "close") return;
                const auto response_connection = header_value(response.headers, "Connection");
                if (response_connection == "close") return;
            }
        }
    }

    void serve() {
        while (running.load()) {
            sockaddr_in addr{};
#ifdef _WIN32
            int len = sizeof(addr);
#else
            socklen_t len = sizeof(addr);
#endif
            socket_t client = ::accept(listen_socket, reinterpret_cast<sockaddr*>(&addr), &len);
            if (client == invalid_socket) {
                if (!running.load()) break;
                std::cerr << "[HTTP] accept failed" << std::endl;
                continue;
            }
            serve_client(client, peer_address(addr));
            close_socket(client);
        }
    }
};

HttpServer::HttpServer() : impl_(new Impl) {}
HttpServer::~HttpServer() { stop(); delete impl_; }

bool HttpServer::start(std::uint16_t port, HttpHandler handler) {
    return start_per_connection(port, [handler = std::move(handler)] { return handler; });
}

bool HttpServer::start_per_connection(std::uint16_t port, HttpHandlerFactory factory) {
    if (impl_->running.exchange(true)) return false;
#ifdef _WIN32
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) { impl_->running = false; return false; }
#endif
    impl_->factory = std::move(factory);
    impl_->retained_handlers.clear();
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

    sockaddr_in actual{};
#ifdef _WIN32
    int actual_len = sizeof(actual);
#else
    socklen_t actual_len = sizeof(actual);
#endif
    if (::getsockname(impl_->listen_socket, reinterpret_cast<sockaddr*>(&actual), &actual_len) == 0) {
        impl_->bound_port = ntohs(actual.sin_port);
    } else {
        impl_->bound_port = port;
    }

    std::cerr << "[HTTP] listening on 0.0.0.0:" << impl_->bound_port << std::endl;
    impl_->thread = std::thread([this] { impl_->serve(); });
    return true;
}

void HttpServer::stop() {
    if (!impl_ || !impl_->running.exchange(false)) return;
    close_socket(impl_->listen_socket);
    impl_->listen_socket = invalid_socket;
    impl_->bound_port = 0;
    if (impl_->thread.joinable()) impl_->thread.join();
    impl_->retained_handlers.clear();
#ifdef _WIN32
    WSACleanup();
#endif
}

bool HttpServer::running() const noexcept { return impl_->running.load(); }
std::uint16_t HttpServer::port() const noexcept { return impl_->bound_port; }

} // namespace gwl::airplay2
