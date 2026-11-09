#include "EventDrivenServer.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <sys/socket.h>
#include <vector>

#include "http/HttpRequest.hpp"

namespace {

using Clock = event::Connection::Clock;

constexpr http::RequestLimits kLimits{
    .max_header_bytes = 8 * 1024,
    .max_body_bytes = 1024 * 1024,
};
// Enough for any single valid request; reading stops there until it's handled.
constexpr size_t kMaxBuffered = kLimits.max_header_bytes + kLimits.max_body_bytes;

// Timeouts are checked on each tick, so a connection closes up to one tick
// late. Tick at a quarter of the shortest timeout, between 10 ms and 1 s.
std::chrono::milliseconds tickInterval(const ServerConfig& config) {
    using std::chrono::milliseconds;
    const milliseconds shortest = std::min(config.idle_timeout, config.request_timeout);
    return std::clamp(shortest / 4, milliseconds(10), milliseconds(1000));
}

int statusFor(http::ParseStatus status) {
    switch (status) {
        case http::ParseStatus::HeadersTooLarge: return 431;
        case http::ParseStatus::BodyTooLarge:    return 413;
        case http::ParseStatus::NotImplemented:  return 501;
        default:                                 return 400;
    }
}

http::HttpResponse errorResponse(int status) {
    http::HttpResponse resp;
    resp.setStatus(status)
        .setContentType("text/plain")
        .setBody("Error " + std::to_string(status) + "\n");
    return resp;
}

} // namespace

EventDrivenServer::EventDrivenServer(ServerConfig config)
    : config_(std::move(config))
    , handler_(config_.doc_root) {}

void EventDrivenServer::start() {
    // Create and configure listener socket as class member (keeps it alive)
    listener_.emplace(config_.port);
    listen_fd_ = listener_->fd();

    // Make listener non-blocking
    int flags = fcntl(listen_fd_, F_GETFL, 0);
    fcntl(listen_fd_, F_SETFL, flags | O_NONBLOCK);

    loop_.add(listen_fd_, [this](int, event::EventType) { onAccept(); });
    loop_.setTick(tickInterval(config_), [this] { onTick(); });

    std::cout << "Event-driven HTTP Server listening on port "
              << config_.port << std::endl;

    loop_.run();
}

void EventDrivenServer::stop() {
    loop_.stop();
}

void EventDrivenServer::onAccept() {
    while (true) {
        int client_fd = ::accept(listen_fd_, nullptr, nullptr);

        if (client_fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return;  // No more pending connections
            }
            if (errno == EINTR || errno == ECONNABORTED) {
                continue;
            }
            if (errno == EMFILE || errno == ENFILE) {
                // Out of file descriptors. The pending connection keeps the
                // listener readable, so a level-triggered loop would spin at
                // 100% CPU. Stop watching it until the next tick.
                std::cerr << "accept: out of file descriptors; pausing accepts\n";
                loop_.modify(listen_fd_, false, false);
                accept_paused_ = true;
                return;
            }
            std::cerr << "accept: " << std::strerror(errno) << "\n";
            return;
        }

        // Owns client_fd from here, so every early exit closes it.
        auto conn = std::make_unique<event::Connection>(net::TcpSocket(client_fd));
        try {
            loop_.add(client_fd, [this](int fd, event::EventType type) {
                onConnectionEvent(fd, type);
            });
        } catch (const std::exception& e) {
            std::cerr << "register fd " << client_fd << ": " << e.what() << "\n";
            continue;
        }
        connections_[client_fd] = std::move(conn);
    }
}

void EventDrivenServer::onConnectionEvent(int fd, event::EventType type) {
    auto it = connections_.find(fd);
    if (it == connections_.end()) return;

    event::Connection& conn = *it->second;

    if (type == event::EventType::Read && conn.state() == event::ConnectionState::Reading) {
        conn.readAvailable(kMaxBuffered);
    } else if (type == event::EventType::Write && conn.state() == event::ConnectionState::Writing) {
        conn.flush();
    }

    serve(conn);
}

// Answers buffered requests in order until more input is needed or a response
// can't be sent in full right now, then watches the events that state needs:
// Reading -> readable only, Writing -> writable only. Not reading while a
// response is in flight keeps a later request from being handled out of turn.
void EventDrivenServer::serve(event::Connection& conn) {
    while (conn.state() == event::ConnectionState::Reading) {
        http::ParseResult parsed = http::parseRequestFrame(conn.input(), kLimits);

        if (parsed.status == http::ParseStatus::Incomplete) {
            if (conn.peerClosed()) {
                conn.close();  // Client is done sending; nothing left to answer
            }
            break;
        }

        if (parsed.status == http::ParseStatus::Complete) {
            conn.consumeInput(parsed.consumed);
            const http::HttpRequest& req = parsed.request;
            std::cout << "  " << req.method << " " << req.path << "\n";
            respond(conn, handler_.handle(req), http::wantsKeepAlive(req));
        } else {
            // Malformed or over a limit: there's no reliable way to find where
            // the next request starts, so reply and close.
            respond(conn, errorResponse(statusFor(parsed.status)), false);
        }
    }

    const int fd = conn.fd();
    const bool reading = conn.state() == event::ConnectionState::Reading;
    const bool writing = conn.state() == event::ConnectionState::Writing;
    if (conn.shouldClose() || !loop_.modify(fd, reading, writing)) {
        closeConnection(fd);  // Destroys conn
    }
}

void EventDrivenServer::respond(event::Connection& conn, http::HttpResponse resp, bool keep_alive) {
    resp.setHeader("Connection", keep_alive ? "keep-alive" : "close");
    conn.setResponse(resp.toString(), !keep_alive);
    // Most responses fit in the socket buffer, so try sending now instead of
    // waiting a loop iteration for EVFILT_WRITE.
    conn.flush();
}

void EventDrivenServer::onTick() {
    if (accept_paused_) {
        accept_paused_ = false;
        loop_.modify(listen_fd_, true, false);
    }

    const auto now = Clock::now();
    std::vector<int> expired;
    for (const auto& [fd, conn] : connections_) {
        const bool mid_request = conn->state() == event::ConnectionState::Reading
                                 && !conn->input().empty();
        if (mid_request ? now - conn->requestStart() > config_.request_timeout
                        : now - conn->lastActivity() > config_.idle_timeout) {
            expired.push_back(fd);
        }
    }
    for (int fd : expired) {
        closeConnection(fd);
    }
}

void EventDrivenServer::closeConnection(int fd) {
    loop_.remove(fd);
    connections_.erase(fd);  // Connection's socket closes the fd
}
