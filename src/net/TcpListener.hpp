#ifndef CPPHTTPSERVER_TCPLISTENER_HPP
#define CPPHTTPSERVER_TCPLISTENER_HPP

#include "TcpSocket.hpp"
#include <string>

namespace net {

/**
 * RAII wrapper for a TCP server socket (listener).
 */
class TcpListener {
public:
    explicit TcpListener(int port, int backlog = 128);
    ~TcpListener();

    // Move-only
    TcpListener(const TcpListener&) = delete;
    TcpListener& operator=(const TcpListener&) = delete;
    TcpListener(TcpListener&& other) noexcept;
    TcpListener& operator=(TcpListener&& other) noexcept;

    [[nodiscard]] int port() const;
    [[nodiscard]] int fd() const { return fd_; }

    TcpSocket accept(std::string& client_ip, int& client_port) const;
    TcpSocket accept();

private:
    int fd_ = -1;
    int port_;
};

} // namespace net

#endif //CPPHTTPSERVER_TCPLISTENER_HPP
