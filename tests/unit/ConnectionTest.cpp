#include "src/event/Connection.hpp"

#include <gtest/gtest.h>
#include <cerrno>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

using event::Connection;
using event::ConnectionState;

namespace {

// A Connection on one end of a socketpair; the test plays the client on `peer`.
class ConnectionTest : public testing::Test {
protected:
    void SetUp() override {
        int fds[2];
        ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
        conn_ = std::make_unique<Connection>(net::TcpSocket(fds[0]));
        peer_ = fds[1];
    }

    void TearDown() override {
        conn_.reset();
        if (peer_ >= 0) ::close(peer_);
    }

    void peerSend(const std::string& data) {
        ASSERT_EQ(::send(peer_, data.data(), data.size(), 0), static_cast<ssize_t>(data.size()));
    }

    std::string peerReceiveAvailable() {
        std::string out;
        char buf[65536];
        while (true) {
            ssize_t n = ::recv(peer_, buf, sizeof(buf), MSG_DONTWAIT);
            if (n <= 0) break;
            out.append(buf, static_cast<size_t>(n));
        }
        return out;
    }

    std::unique_ptr<Connection> conn_;
    int peer_ = -1;
};

} // namespace

TEST_F(ConnectionTest, ReadAvailableBuffersInput) {
    peerSend("hello ");
    peerSend("world");
    conn_->readAvailable(1024);
    EXPECT_EQ(conn_->input(), "hello world");
    EXPECT_EQ(conn_->state(), ConnectionState::Reading);
    EXPECT_FALSE(conn_->peerClosed());
}

TEST_F(ConnectionTest, ReadAvailableWithNothingToReadIsANoop) {
    conn_->readAvailable(1024);  // Must not block: socket is non-blocking
    EXPECT_TRUE(conn_->input().empty());
    EXPECT_EQ(conn_->state(), ConnectionState::Reading);
}

TEST_F(ConnectionTest, ReadAvailableStopsAtLimit) {
    // Socket-pair buffers default to ~8 KB on macOS; enlarge them so the peer
    // can queue more than one 16 KB recv() chunk without blocking.
    int size = 256 * 1024;
    ::setsockopt(peer_, SOL_SOCKET, SO_SNDBUF, &size, sizeof(size));
    ::setsockopt(conn_->fd(), SOL_SOCKET, SO_RCVBUF, &size, sizeof(size));

    const std::string data(200000, 'x');
    const ssize_t queued = ::send(peer_, data.data(), data.size(), MSG_DONTWAIT);
    ASSERT_GT(queued, 40000) << "couldn't queue enough data to test the limit";

    conn_->readAvailable(1000);
    // Stops once the limit is crossed, after at most one extra 16 KB chunk.
    EXPECT_GE(conn_->input().size(), 1000u);
    EXPECT_LE(conn_->input().size(), 1000u + 16384u);
}

TEST_F(ConnectionTest, ConsumeInputDropsOnlyThePrefix) {
    peerSend("first|second");
    conn_->readAvailable(1024);
    conn_->consumeInput(6);
    EXPECT_EQ(conn_->input(), "second");
}

TEST_F(ConnectionTest, EofSetsPeerClosedButKeepsBufferedData) {
    peerSend("partial");
    ::shutdown(peer_, SHUT_WR);
    conn_->readAvailable(1024);
    EXPECT_TRUE(conn_->peerClosed());
    EXPECT_EQ(conn_->input(), "partial");
    EXPECT_NE(conn_->state(), ConnectionState::Closed);  // Server decides when to close
}

TEST_F(ConnectionTest, FlushSendsResponseAndReturnsToReading) {
    conn_->setResponse("RESPONSE", /*close_after=*/false);
    EXPECT_EQ(conn_->state(), ConnectionState::Writing);
    conn_->flush();
    EXPECT_EQ(conn_->state(), ConnectionState::Reading);
    EXPECT_EQ(peerReceiveAvailable(), "RESPONSE");
}

TEST_F(ConnectionTest, FlushWithCloseAfterCloses) {
    conn_->setResponse("BYE", /*close_after=*/true);
    conn_->flush();
    EXPECT_TRUE(conn_->shouldClose());
    EXPECT_EQ(peerReceiveAvailable(), "BYE");
}

TEST_F(ConnectionTest, LargeResponseIsSentAcrossMultipleFlushes) {
    const std::string big(4 * 1024 * 1024, 'z');
    conn_->setResponse(big, false);

    conn_->flush();  // Fills the socket buffer, then hits EAGAIN
    EXPECT_EQ(conn_->state(), ConnectionState::Writing);

    std::string received;
    for (int i = 0; i < 10000 && conn_->state() == ConnectionState::Writing; ++i) {
        received += peerReceiveAvailable();
        conn_->flush();
    }
    received += peerReceiveAvailable();

    EXPECT_EQ(conn_->state(), ConnectionState::Reading);
    EXPECT_EQ(received.size(), big.size());
    EXPECT_EQ(received, big);
}

TEST_F(ConnectionTest, FlushToClosedPeerClosesInsteadOfRaisingSigpipe) {
    ::close(peer_);
    peer_ = -1;
    conn_->setResponse(std::string(1 << 20, 'x'), false);
    conn_->flush();  // Would kill the test process with SIGPIPE without SO_NOSIGPIPE
    EXPECT_TRUE(conn_->shouldClose());
}
