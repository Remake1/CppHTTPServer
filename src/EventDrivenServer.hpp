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

    ServerConfig config_;
    handlers::StaticFileHandler handler_;
    event::EventLoop loop_;
    std::optional<net::TcpListener> listener_;  // Must persist while loop runs
    int listen_fd_ = -1;
    bool accept_paused_ = false;  // Set while out of file descriptors

    std::unordered_map<int, std::unique_ptr<event::Connection>> connections_;
};


#endif //CPPHTTPSERVER_EVENTDRIVENSERVER_HPP
