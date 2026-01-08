#include "TcpSocket.hpp"
#include <sys/socket.h>
#include <unistd.h>

namespace net {

TcpSocket::TcpSocket(int fd) : fd_(fd) {}

TcpSocket::~TcpSocket() {
    close();
}

TcpSocket::TcpSocket(TcpSocket&& other) noexcept : fd_(other.fd_) {
    other.fd_ = -1;
}

TcpSocket& TcpSocket::operator=(TcpSocket&& other) noexcept {
    if (this != &other) {
        close();
        fd_ = other.fd_;
        other.fd_ = -1;
    }
    return *this;
}

int TcpSocket::fd() const { return fd_; }

bool TcpSocket::valid() const { return fd_ >= 0; }

std::string TcpSocket::recv(size_t max_len) {
    std::string buffer(max_len, '\0');
    ssize_t n = ::recv(fd_, buffer.data(), max_len, 0);
    if (n <= 0) return "";
    buffer.resize(n);
    return buffer;
}

std::string TcpSocket::readHttpRequest() {
    std::string request;
    while (request.find("\r\n\r\n") == std::string::npos) {
        std::string chunk = recv();
        if (chunk.empty()) break;
        request += chunk;
    }
    return request;
}

bool TcpSocket::sendAll(const std::string& data) {
    size_t sent_total = 0;
    while (sent_total < data.size()) {
        ssize_t sent = ::send(fd_, data.data() + sent_total,
                               data.size() - sent_total, 0);
        if (sent <= 0) return false;
        sent_total += sent;
    }
    return true;
}

void TcpSocket::shutdown() const {
    if (fd_ >= 0) {
        ::shutdown(fd_, SHUT_WR);
    }
}

void TcpSocket::close() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

} // namespace net
