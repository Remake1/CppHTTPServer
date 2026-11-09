#include "HttpServer.hpp"
#include "net/TcpListener.hpp"
#include "http/HttpRequest.hpp"
#include "http/HttpResponse.hpp"
#include <iostream>
#include <mutex>
#include <utility>

#include "concurrency/ThreadPool.hpp"

namespace {

std::mutex log_mutex;

// std::cout is shared by the accept thread and every worker. The standard
// permits concurrent writes (output may interleave), but libc++ updates the
// stream's formatting state without synchronisation, which ThreadSanitizer
// reports as a data race. Serialise whole lines instead.
template <typename... Args>
void logLine(const Args&... args) {
    std::lock_guard lock(log_mutex);
    (std::cout << ... << args) << std::endl;
}

} // namespace

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
            logLine("New connection from ", client_ip, ":", client_port);

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

        logLine("  ", req.method, " ", req.path);

        const bool keep_alive = http::wantsKeepAlive(req);

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
