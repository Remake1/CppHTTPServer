#ifndef CPPHTTPSERVER_EVENTDRIVENSERVER_HPP
#define CPPHTTPSERVER_EVENTDRIVENSERVER_HPP


#include "event/EventLoop.hpp"
#include "event/Connection.hpp"
#include "handlers/StaticFileHandler.hpp"
#include "http/HttpResponse.hpp"
#include "net/TcpListener.hpp"
#include <unordered_map>
#include <memory>
#include <optional>

#include "HttpServer.hpp" // For ServerConfig

class EventDrivenServer {
public:
    explicit EventDrivenServer(ServerConfig config);

    void start();
    void stop();  // Thread-safe
private:
    void onAccept();
    void onConnectionEvent(int fd, event::EventType type);
    void serve(event::Connection& conn);
    void respond(event::Connection& conn, http::HttpResponse resp, bool keep_alive);
    void onTick();
    void closeConnection(int fd);
    void pauseAccepting();
    void resumeAccepting();

    ServerConfig config_;
    handlers::StaticFileHandler handler_;
    event::EventLoop loop_;
    std::optional<net::TcpListener> listener_;  // Must persist while loop runs
    int listen_fd_ = -1;
    size_t max_connections_ = 0;  // Derived from the process's fd limit
    bool accept_paused_ = false;  // At max_connections_, or out of fds

    std::unordered_map<int, std::unique_ptr<event::Connection>> connections_;
};


#endif //CPPHTTPSERVER_EVENTDRIVENSERVER_HPP
