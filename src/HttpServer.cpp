#include "HttpServer.hpp"
#include "net/TcpListener.hpp"
#include "http/HttpRequest.hpp"
#include "http/HttpResponse.hpp"
#include <iostream>
#include <utility>

HttpServer::HttpServer(ServerConfig config)
    : config_(std::move(config))
    , handler_(config_.doc_root) {}

void HttpServer::start() {
    net::TcpListener listener(config_.port);

    std::cout << "HTTP Server listening on port " << config_.port << std::endl;
    std::cout << "Document root: " << config_.doc_root << std::endl;
    std::cout << "Waiting for connections...\n";

    while (running_) {
        try {
            int client_port;
            std::string client_ip;
            net::TcpSocket client = listener.accept(client_ip, client_port);
            std::cout << "New connection from " << client_ip << ":" << client_port << std::endl;

            handleConnection(std::move(client));
        } catch (const std::exception& e) {
            std::cerr << "Accept error: " << e.what() << std::endl;
        }
    }
}

void HttpServer::stop() {
    running_ = false;
}

void HttpServer::handleConnection(net::TcpSocket client) {
    // Read request
    std::string raw = client.readHttpRequest();
    if (raw.empty()) {
        return;
    }

    // Parse request
    http::HttpRequest req = http::parseHttpRequest(raw);

    std::cout << "  " << req.method << " " << req.path << std::endl;

    // Handle request
    http::HttpResponse resp = handler_.handle(req);

    // Send response
    client.sendAll(resp.toString());

    // Graceful shutdown
    client.shutdown();
    // Socket closes automatically via RAII
}
