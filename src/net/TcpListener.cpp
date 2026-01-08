#include "TcpListener.hpp"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdexcept>

namespace net {

TcpListener::TcpListener(int port, int backlog) : port_(port) {
    fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (fd_ < 0) {
        throw std::runtime_error("Failed to create socket");
    }

    int opt = 1;
    if (setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        ::close(fd_);
        throw std::runtime_error("Failed to set socket options");
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(fd_);
        throw std::runtime_error("Failed to bind socket");
    }

    if (listen(fd_, backlog) < 0) {
        ::close(fd_);
        throw std::runtime_error("Failed to listen on socket");
    }
}

TcpListener::~TcpListener() {
    if (fd_ >= 0) {
        ::close(fd_);
    }
}

TcpListener::TcpListener(TcpListener&& other) noexcept
    : fd_(other.fd_), port_(other.port_) {
    other.fd_ = -1;
}

TcpListener& TcpListener::operator=(TcpListener&& other) noexcept {
    if (this != &other) {
        if (fd_ >= 0) ::close(fd_);
        fd_ = other.fd_;
        port_ = other.port_;
        other.fd_ = -1;
    }
    return *this;
}

int TcpListener::port() const { return port_; }

TcpSocket TcpListener::accept(std::string& client_ip, int& client_port) const {
    sockaddr_in client_addr{};
    socklen_t len = sizeof(client_addr);

    int client_fd = ::accept(fd_, reinterpret_cast<sockaddr*>(&client_addr), &len);
    if (client_fd < 0) {
        throw std::runtime_error("Failed to accept connection");
    }

    client_ip = inet_ntoa(client_addr.sin_addr);
    client_port = ntohs(client_addr.sin_port);

    return TcpSocket(client_fd);
}

TcpSocket TcpListener::accept() {
    std::string ip;
    int port;
    return accept(ip, port);
}

} // namespace net
