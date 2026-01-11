#include "Connection.hpp"

#include <sys/socket.h>

namespace event {

Connection::Connection(net::TcpSocket socket)
    : socket_(std::move(socket)) {
    socket_.setNonBlocking();
}

bool Connection::tryRead() {
    char buffer[4096];

    while (true) {
        ssize_t n = ::recv(socket_.fd(), buffer, sizeof(buffer), 0);

        if (n > 0) {
            read_buffer_.append(buffer, n);

            // Check if we have complete HTTP headers
            if (read_buffer_.find("\r\n\r\n") != std::string::npos) {
                state_ = ConnectionState::Processing;
                return true;  // Request complete
            }
        }
        else if (n == 0) {
            // Connection closed by peer
            state_ = ConnectionState::Closed;
            return false;
        }
        else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // No more data available right now
                return false;  // Request not yet complete
            }
            // Real error
            state_ = ConnectionState::Closed;
            return false;
        }
    }
}

bool Connection::tryWrite() {
    while (write_pos_ < write_buffer_.size()) {
        ssize_t n = ::send(socket_.fd(),
                          write_buffer_.data() + write_pos_,
                          write_buffer_.size() - write_pos_,
                          0);

        if (n > 0) {
            write_pos_ += n;
        }
        else if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Socket buffer full, try again later
                return false;  // Write not complete
            }
            // Real error
            state_ = ConnectionState::Closed;
            return false;
        }
    }

    // Write complete - reset for keep-alive or mark closed
    write_buffer_.clear();
    write_pos_ = 0;
    read_buffer_.clear();
    state_ = ConnectionState::Reading;  // Ready for next request
    return true;
}

void Connection::setResponse(const std::string &response) {
    write_buffer_ = response;
    write_pos_ = 0;
    state_ = ConnectionState::Writing;
}
} // namespace event