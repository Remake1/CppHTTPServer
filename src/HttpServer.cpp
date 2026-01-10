#include "HttpServer.hpp"
#include "net/TcpListener.hpp"
#include "http/HttpRequest.hpp"
#include "http/HttpResponse.hpp"
#include <iostream>
#include <utility>

#include "concurrency/ThreadPool.hpp"

HttpServer::HttpServer(ServerConfig config)
    : config_(std::move(config))
    , handler_(config_.doc_root) {}

void HttpServer::start() {
    net::TcpListener listener(config_.port);

    ThreadPool thread_pool(config_.thread_pool_size);

    std::cout << "HTTP Server listening on port " << config_.port << std::endl;
    std::cout << "Document root: " << config_.doc_root << std::endl;
    std::cout << "Waiting for connections...\n";

    while (running_) {
        try {
            int client_port;
            std::string client_ip;
            net::TcpSocket client = listener.accept(client_ip, client_port);
            std::cout << "New connection from " << client_ip << ":" << client_port << std::endl;

            auto client_ptr = std::make_shared<net::TcpSocket>(std::move(client));

            thread_pool.submit([this, client_ptr]() mutable {
                handleConnection(std::move(*client_ptr));
            });

        } catch (const std::exception& e) {
            std::cerr << "Accept error: " << e.what() << std::endl;
        }
    }
}

void HttpServer::stop() {
    running_ = false;
}

static bool isKeepAliveRequested(const http::HttpRequest& req) {
    // RFC 7230-ish behavior:
    // - HTTP/1.1 defaults to keep-alive unless Connection: close
    // - HTTP/1.0 defaults to close unless Connection: keep-alive
    auto it = req.headers.find("connection");
    const bool is_http_1_0 = (req.version == "HTTP/1.0");

    if (it == req.headers.end()) {
        return !is_http_1_0;
    }

    const std::string& v = it->second;
    if (v.find("close") != std::string::npos) return false;
    if (v.find("keep-alive") != std::string::npos) return true;

    return !is_http_1_0;
}

void HttpServer::handleConnection(net::TcpSocket client) {
    // Persistent connection loop: handle multiple requests per TCP connection.
    while (client.valid()) {
        std::string raw = client.readHttpRequest();
        if (raw.empty()) {
            // Client closed or read timeout/error.
            break;
        }

        // Parse request
        http::HttpRequest req = http::parseHttpRequest(raw);
        if (req.method.empty() || req.path.empty()) {
            http::HttpResponse bad;
            bad.setStatus(400)
               .setContentType("text/plain")
               .setHeader("Connection", "close")
               .setBody("400 Bad Request\n");
            (void)client.sendAll(bad.toString());
            break;
        }

        std::cout << "  " << req.method << " " << req.path << std::endl;

        const bool keep_alive = isKeepAliveRequested(req);

        // Handle request
        http::HttpResponse resp = handler_.handle(req);
        resp.setHeader("Connection", keep_alive ? "keep-alive" : "close");

        if (!client.sendAll(resp.toString())) {
            break;
        }

        if (!keep_alive) {
            break;
        }
    }

    client.shutdown();
}
