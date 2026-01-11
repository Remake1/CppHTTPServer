#include "EventDrivenServer.hpp"

#include <iostream>
#include <netinet/in.h>
#include <sys/fcntl.h>
#include <sys/socket.h>

#include "net/TcpListener.hpp"

EventDrivenServer::EventDrivenServer(ServerConfig config)
    : config_(std::move(config))
    , handler_(config_.doc_root) {}

void EventDrivenServer::start() {
    // Create and configure listener socket as class member (keeps it alive)
    listener_.emplace(config_.port);
    int listen_fd = listener_->fd();

    // Make listener non-blocking
    int flags = fcntl(listen_fd, F_GETFL, 0);
    fcntl(listen_fd, F_SETFL, flags | O_NONBLOCK);

    std::cout << "Event-driven HTTP Server listening on port "
              << config_.port << std::endl;

    // Register accept callback
    loop_.addRead(listen_fd, [this](int fd, event::EventType type) {
        onAccept(fd, type);
    });

    // Run the event loop
    loop_.run();
}

void EventDrivenServer::stop() {
    loop_.stop();
}

void EventDrivenServer::onAccept(int fd, event::EventType type) {
    while (true) {
        sockaddr_in addr{};
        socklen_t len = sizeof(addr);
        int client_fd = ::accept(fd, reinterpret_cast<sockaddr *>(&addr), &len);

        if (client_fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // No more pending connections
                break;
            }
            std::cerr << "Accept error\n";
            break;
        }

        std::cout << "New connection: fd=" << client_fd << std::endl;

        // Create connection
        auto conn = std::make_unique<event::Connection>(
            net::TcpSocket(client_fd)
        );

        int cfd = conn->fd();
        connections_[cfd] = std::move(conn);

        // Register for read events
        loop_.addRead(cfd, [this](int fd, event::EventType type) {
            onRead(fd, type);
        });
    }
}

void EventDrivenServer::onRead(int fd, event::EventType type) {
    auto it = connections_.find(fd);
    if (it == connections_.end()) return;

    auto& conn = *it->second;

    if (conn.tryRead()) {
        // Request complete, process it
        processRequest(conn);
    }

    if (conn.shouldClose()) {
        closeConnection(fd);
    }
}

void EventDrivenServer::onWrite(int fd, event::EventType type) {
    auto it = connections_.find(fd);
    if (it == connections_.end()) return;

    auto& conn = *it->second;

    if (conn.tryWrite()) {
        // Write complete - disable write events, enable read for next request (keep-alive)
        loop_.disableWrite(fd);
        // Connection is now back in Reading state, ready for next request
    }

    if (conn.shouldClose()) {
        closeConnection(fd);
    }
}

void EventDrivenServer::processRequest(event::Connection& conn) {
    // Parse request
    http::HttpRequest req = http::parseHttpRequest(conn.requestData());

    if (req.method.empty()) {
        http::HttpResponse bad;
        bad.setStatus(400).setBody("400 Bad Request\n");
        conn.setResponse(bad.toString());
    } else {
        std::cout << "  " << req.method << " " << req.path << std::endl;
        http::HttpResponse resp = handler_.handle(req);
        conn.setResponse(resp.toString());
    }

    // Register for write events
    loop_.addWrite(conn.fd(), [this](int fd, event::EventType type) {
        onWrite(fd, type);
    });
}
void EventDrivenServer::closeConnection(int fd) {
    loop_.remove(fd);
    connections_.erase(fd);
}


