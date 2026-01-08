#ifndef CPPHTTPSERVER_TCPSOCKET_HPP
#define CPPHTTPSERVER_TCPSOCKET_HPP

#include <string>

namespace net {

/**
 * RAII wrapper for a TCP socket.
 */
class TcpSocket {
public:
    explicit TcpSocket(int fd);
    ~TcpSocket();

    // Move-only
    TcpSocket(const TcpSocket&) = delete;
    TcpSocket& operator=(const TcpSocket&) = delete;
    TcpSocket(TcpSocket&& other) noexcept;
    TcpSocket& operator=(TcpSocket&& other) noexcept;

    [[nodiscard]] int fd() const;
    [[nodiscard]] bool valid() const;

    std::string recv(size_t max_len = 4096);
    std::string readHttpRequest();
    bool sendAll(const std::string& data);
    void shutdown() const;
    void close();

private:
    int fd_ = -1;
};

} // namespace net

#endif //CPPHTTPSERVER_TCPSOCKET_HPP
