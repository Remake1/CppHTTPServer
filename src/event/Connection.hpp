#ifndef CPPHTTPSERVER_CONNECTION_HPP
#define CPPHTTPSERVER_CONNECTION_HPP


#include "../net/TcpSocket.hpp"
#include <chrono>
#include <string>
#include <string_view>

namespace event {

enum class ConnectionState {
    Reading,  // Waiting for (the rest of) a request
    Writing,  // Sending a response; no reads until it is fully sent
    Closed,
};

// Buffers and state for one client connection. Owns no event-loop logic:
// the server decides what to do with requests and which events to watch.
class Connection {
public:
    using Clock = std::chrono::steady_clock;

    explicit Connection(net::TcpSocket socket);

    int fd() const { return socket_.fd(); }
    ConnectionState state() const { return state_; }
    bool shouldClose() const { return state_ == ConnectionState::Closed; }

    // Reads everything currently available, stopping once the input buffer
    // holds max_buffered bytes. EOF sets peerClosed(); an error closes.
    void readAvailable(size_t max_buffered);

    // True once the client has sent EOF. Buffered requests may still be served.
    bool peerClosed() const { return peer_closed_; }

    // Unprocessed input: may hold a partial request or several pipelined ones.
    std::string_view input() const { return read_buffer_; }

    // Drops the first n bytes of input (one request that has been handled).
    void consumeInput(size_t n);

    // Queues a response and enters Writing. If close_after is set, the
    // connection closes once the response has been fully sent.
    void setResponse(std::string response, bool close_after);

    // Sends as much of the pending response as the socket accepts. When it is
    // fully sent, returns to Reading, or closes if close_after was set.
    void flush();

    void close() { state_ = ConnectionState::Closed; }

    // Time of the last successful read or write.
    Clock::time_point lastActivity() const { return last_activity_; }

    // When the first byte of the request now in the input buffer arrived.
    // Only meaningful while input() is non-empty.
    Clock::time_point requestStart() const { return request_start_; }

private:
    net::TcpSocket socket_;
    ConnectionState state_ = ConnectionState::Reading;
    bool peer_closed_ = false;
    bool close_after_write_ = false;
    std::string read_buffer_;
    std::string write_buffer_;
    size_t write_pos_ = 0;
    Clock::time_point last_activity_;
    Clock::time_point request_start_;
};

} // namespace event


#endif //CPPHTTPSERVER_CONNECTION_HPP
