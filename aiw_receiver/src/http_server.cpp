#include "http_server.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <sstream>
#include <stdexcept>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using socket_t = SOCKET;
static void close_socket(socket_t s) { closesocket(s); }
#define AIW_SEND_FLAGS 0
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
using socket_t = int;
static void close_socket(socket_t s) { ::close(s); }
#ifdef MSG_NOSIGNAL
#define AIW_SEND_FLAGS MSG_NOSIGNAL
#else
#define AIW_SEND_FLAGS 0
#endif
#endif

namespace aiw {

namespace {

const char* reason(int status) {
    switch (status) {
        case 200: return "OK";
        case 400: return "Bad Request";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 413: return "Payload Too Large";
        default: return "Error";
    }
}

bool send_all(socket_t s, const std::string& data) {
    std::size_t off = 0;
    while (off < data.size()) {
        const auto n = ::send(s, data.data() + off, static_cast<int>(data.size() - off), AIW_SEND_FLAGS);
        if (n <= 0) return false;
        off += static_cast<std::size_t>(n);
    }
    return true;
}

// Waits up to `ms` for the socket to become readable.
bool readable(socket_t s, int ms) {
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(s, &fds);
    timeval tv{ms / 1000, (ms % 1000) * 1000};
    return ::select(static_cast<int>(s) + 1, &fds, nullptr, nullptr, &tv) > 0;
}

}  // namespace

HttpServer::HttpServer(std::string bind_addr, int port, Handler handler)
    : bind_addr_(std::move(bind_addr)), port_(port), handler_(std::move(handler)) {}

HttpServer::~HttpServer() { stop(); }

void HttpServer::start() {
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) throw std::runtime_error("WSAStartup failed");
#endif
    socket_t s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
#ifdef _WIN32
    if (s == INVALID_SOCKET) throw std::runtime_error("socket() failed");
#else
    if (s < 0) throw std::runtime_error("socket() failed");
    int yes = 1;
    ::setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
#endif
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<unsigned short>(port_));
    if (::inet_pton(AF_INET, bind_addr_.c_str(), &addr.sin_addr) != 1) {
        close_socket(s);
        throw std::runtime_error("invalid --ui-bind address " + bind_addr_);
    }
    if (::bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 || ::listen(s, 16) != 0) {
        close_socket(s);
        throw std::runtime_error("cannot listen on " + bind_addr_ + ":" + std::to_string(port_) +
                                 " (port in use? try --ui-port)");
    }
    listen_fd_ = static_cast<long long>(s);
    running_ = true;
    thread_ = std::thread([this] { loop(); });
}

void HttpServer::stop() {
    if (!running_.exchange(false)) return;
    if (thread_.joinable()) thread_.join();
    close_socket(static_cast<socket_t>(listen_fd_));
#ifdef _WIN32
    WSACleanup();
#endif
}

void HttpServer::loop() {
    const auto ls = static_cast<socket_t>(listen_fd_);
    while (running_) {
        if (!readable(ls, 200)) continue;
        const socket_t c = ::accept(ls, nullptr, nullptr);
#ifdef _WIN32
        if (c == INVALID_SOCKET) continue;
#else
        if (c < 0) continue;
#endif
        serve_one(static_cast<long long>(c));
        close_socket(c);
    }
}

void HttpServer::serve_one(long long client) {
    const auto c = static_cast<socket_t>(client);
    constexpr std::size_t MAX_REQ = 64 * 1024;
    std::string data;
    char buf[4096];
    std::size_t header_end = std::string::npos;
    while (header_end == std::string::npos) {
        if (!readable(c, 2000)) return;
        const auto n = ::recv(c, buf, sizeof(buf), 0);
        if (n <= 0) return;
        data.append(buf, static_cast<std::size_t>(n));
        header_end = data.find("\r\n\r\n");
        if (data.size() > MAX_REQ) return;
    }

    HttpRequest req;
    std::istringstream hs(data.substr(0, header_end));
    std::string line, version, target;
    std::getline(hs, line);
    std::istringstream rl(line);
    rl >> req.method >> target >> version;
    const auto q = target.find('?');
    req.path = target.substr(0, q);
    if (q != std::string::npos) req.query = target.substr(q + 1);
    while (std::getline(hs, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string k = line.substr(0, colon), v = line.substr(colon + 1);
        std::transform(k.begin(), k.end(), k.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        v.erase(0, v.find_first_not_of(' '));
        req.headers[k] = v;
    }

    std::size_t content_len = 0;
    if (auto it = req.headers.find("content-length"); it != req.headers.end())
        content_len = static_cast<std::size_t>(std::strtoul(it->second.c_str(), nullptr, 10));
    HttpResponse resp;
    if (content_len > MAX_REQ) {
        resp = {413, "text/plain", "too large"};
    } else {
        req.body = data.substr(header_end + 4);
        while (req.body.size() < content_len) {
            if (!readable(c, 2000)) return;
            const auto n = ::recv(c, buf, sizeof(buf), 0);
            if (n <= 0) return;
            req.body.append(buf, static_cast<std::size_t>(n));
        }
        req.body.resize(content_len);
        try {
            resp = handler_(req);
        } catch (const std::exception& e) {
            resp = {400, "text/plain", e.what()};
        }
    }

    std::ostringstream out;
    out << "HTTP/1.1 " << resp.status << ' ' << reason(resp.status) << "\r\n"
        << "Content-Type: " << resp.content_type << "\r\n"
        << "Content-Length: " << resp.body.size() << "\r\n"
        << "Cache-Control: no-store\r\n"
        << "X-Content-Type-Options: nosniff\r\n"
        << "Connection: close\r\n\r\n";
    send_all(c, out.str() + resp.body);
}

}  // namespace aiw
