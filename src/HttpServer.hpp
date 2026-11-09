#ifndef CPPHTTPSERVER_HTTPSERVER_HPP
#define CPPHTTPSERVER_HTTPSERVER_HPP

#include "net/TcpSocket.hpp"
#include "handlers/StaticFileHandler.hpp"
#include <chrono>
#include <string>

struct ServerConfig {
    int port = 3000;
    std::string doc_root = "../www";
    size_t thread_pool_size = 4;

    // Event-driven server only: close a connection after this long with no
    // traffic, or if a request it started sending isn't complete in time.
    std::chrono::milliseconds idle_timeout{15'000};
    std::chrono::milliseconds request_timeout{10'000};
};

class HttpServer {
public:
    explicit HttpServer(ServerConfig config = {});

    void start();
    void stop();
    void handleConnection(net::TcpSocket client);

private:
    ServerConfig config_;
    handlers::StaticFileHandler handler_;
    bool running_ = true;
};

#endif //CPPHTTPSERVER_HTTPSERVER_HPP
