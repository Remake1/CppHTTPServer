#ifndef CPPHTTPSERVER_EVENTDRIVENSERVER_HPP
#define CPPHTTPSERVER_EVENTDRIVENSERVER_HPP


#include "event/EventLoop.hpp"
#include "event/Connection.hpp"
#include "handlers/StaticFileHandler.hpp"
#include "net/TcpListener.hpp"
#include <unordered_map>
#include <memory>
#include <optional>

#include "HttpServer.hpp" // For ServerConfig

struct ServerConfig;  // Forward declaration
class EventDrivenServer {
public:
    explicit EventDrivenServer(ServerConfig config);

    void start();
    void stop();
private:
    void onAccept(int fd, event::EventType type);
    void onRead(int fd, event::EventType type);
    void onWrite(int fd, event::EventType type);
    void processRequest(event::Connection& conn);
    void closeConnection(int fd);
    ServerConfig config_;
    handlers::StaticFileHandler handler_;
    event::EventLoop loop_;
    std::optional<net::TcpListener> listener_;  // Must persist while loop runs

    std::unordered_map<int, std::unique_ptr<event::Connection>> connections_;
};


#endif //CPPHTTPSERVER_EVENTDRIVENSERVER_HPP