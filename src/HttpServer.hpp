#ifndef CPPHTTPSERVER_HTTPSERVER_HPP
#define CPPHTTPSERVER_HTTPSERVER_HPP

#include "net/TcpSocket.hpp"
#include "handlers/StaticFileHandler.hpp"
#include <string>

struct ServerConfig {
    int port = 3000;
    std::string doc_root = "../www";
    size_t thread_pool_size = 4;
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
