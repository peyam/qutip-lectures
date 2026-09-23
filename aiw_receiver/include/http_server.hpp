// Minimal HTTP/1.1 server for the local dashboard (no dependencies).
// One background thread; requests are handled sequentially with
// "Connection: close".  Intended for localhost use only.
#pragma once

#include <atomic>
#include <functional>
#include <map>
#include <string>
#include <thread>

namespace aiw {

struct HttpRequest {
    std::string method;
    std::string path;    // without query string
    std::string query;
    std::map<std::string, std::string> headers;  // lower-case keys
    std::string body;
};

struct HttpResponse {
    int status = 200;
    std::string content_type = "application/json";
    std::string body;
};

class HttpServer {
public:
    using Handler = std::function<HttpResponse(const HttpRequest&)>;

    HttpServer(std::string bind_addr, int port, Handler handler);
    ~HttpServer();

    // Throws std::runtime_error if the socket cannot be bound.
    void start();
    void stop();
    int port() const { return port_; }

private:
    void loop();
    void serve_one(long long client);

    std::string bind_addr_;
    int port_;
    Handler handler_;
    long long listen_fd_ = -1;
    std::atomic<bool> running_{false};
    std::thread thread_;
};

}  // namespace aiw
