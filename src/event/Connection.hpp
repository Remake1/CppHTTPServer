#ifndef CPPHTTPSERVER_CONNECTION_HPP
#define CPPHTTPSERVER_CONNECTION_HPP


#include "../net/TcpSocket.hpp"
#include <string>
#include <functional>

namespace event {

enum class ConnectionState {
    Reading,      // Reading request headers
    Processing,   // Parsing request
    Writing,      // Sending response
    Closed,
};

class Connection {
public:
    explicit Connection(net::TcpSocket socket);

    int fd() const { return socket_.fd(); }
    ConnectionState state() const { return state_; }

    // Non-blocking read - returns true if request is complete
    bool tryRead();

    // Non-blocking write - returns true if write is complete
    bool tryWrite();

    // Get the raw request data (when complete)
    const std::string& requestData() const { return read_buffer_; }

    // Set response to send
    void setResponse(const std::string& response);

    // Check if connection should be closed
    bool shouldClose() const { return state_ == ConnectionState::Closed; }
private:
    net::TcpSocket socket_;
    ConnectionState state_ = ConnectionState::Reading;
    std::string read_buffer_;
    std::string write_buffer_;
    size_t write_pos_ = 0;
};

} // namespace event


#endif //CPPHTTPSERVER_CONNECTION_HPP