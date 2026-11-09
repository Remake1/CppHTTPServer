#include "Connection.hpp"

#include <cerrno>
#include <sys/socket.h>

namespace event {

Connection::Connection(net::TcpSocket socket)
    : socket_(std::move(socket))
    , last_activity_(Clock::now())
    , request_start_(last_activity_) {
    socket_.setNonBlocking();
}

void Connection::readAvailable(size_t max_buffered) {
    char buffer[16384];

    while (read_buffer_.size() < max_buffered) {
        ssize_t n = ::recv(socket_.fd(), buffer, sizeof(buffer), 0);

        if (n > 0) {
            const auto now = Clock::now();
            if (read_buffer_.empty()) {
                request_start_ = now;
            }
            read_buffer_.append(buffer, static_cast<size_t>(n));
            last_activity_ = now;
        }
        else if (n == 0) {
            peer_closed_ = true;  // Client sent EOF; it may still read our reply
            return;
        }
        else if (errno == EINTR) {
            continue;
        }
        else if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return;  // No more data available right now
        }
        else {
            close();  // Real error (e.g. ECONNRESET)
            return;
        }
    }
}

void Connection::consumeInput(size_t n) {
    read_buffer_.erase(0, n);
    if (!read_buffer_.empty()) {
        // The next pipelined request is already here; its clock starts now.
        request_start_ = Clock::now();
    }
}

void Connection::setResponse(std::string response, bool close_after) {
    write_buffer_ = std::move(response);
    write_pos_ = 0;
    close_after_write_ = close_after;
    state_ = ConnectionState::Writing;
}

void Connection::flush() {
    while (write_pos_ < write_buffer_.size()) {
        ssize_t n = ::send(socket_.fd(),
                           write_buffer_.data() + write_pos_,
                           write_buffer_.size() - write_pos_,
                           0);

        if (n > 0) {
            write_pos_ += static_cast<size_t>(n);
            last_activity_ = Clock::now();
        }
        else if (n < 0 && errno == EINTR) {
            continue;
        }
        else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return;  // Socket buffer full; wait for EVFILT_WRITE
        }
        else {
            close();  // EPIPE, ECONNRESET, ...
            return;
        }
    }

    write_buffer_.clear();
    write_buffer_.shrink_to_fit();  // Don't hold on to a large file's buffer
    write_pos_ = 0;
    if (close_after_write_) {
        close();
    } else {
        state_ = ConnectionState::Reading;
    }
}

} // namespace event
