#include "src/event/EventLoop.hpp"

#include <gtest/gtest.h>
#include <chrono>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

using event::EventLoop;
using event::EventType;
using namespace std::chrono_literals;

namespace {

struct SocketPair {
    int a = -1, b = -1;
    SocketPair() {
        int fds[2];
        if (::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0) {
            a = fds[0];
            b = fds[1];
        }
    }
    ~SocketPair() {
        if (a >= 0) ::close(a);
        if (b >= 0) ::close(b);
    }
};

} // namespace

TEST(EventLoop, StopFromAnotherThreadWakesTheLoop) {
    EventLoop loop;
    const auto start = std::chrono::steady_clock::now();
    std::thread stopper([&] {
        std::this_thread::sleep_for(50ms);
        loop.stop();
    });
    loop.run();  // Blocks in kevent() with no timeout until stop() wakes it
    stopper.join();
    EXPECT_LT(std::chrono::steady_clock::now() - start, 2s);
}

TEST(EventLoop, StopBeforeRunReturnsImmediately) {
    EventLoop loop;
    loop.stop();
    loop.run();
    SUCCEED();
}

TEST(EventLoop, TickFiresRepeatedly) {
    EventLoop loop;
    int ticks = 0;
    loop.setTick(5ms, [&] {
        if (++ticks == 3) loop.stop();
    });
    loop.run();
    EXPECT_EQ(ticks, 3);
}

TEST(EventLoop, DeliversReadEvent) {
    SocketPair sp;
    EventLoop loop;
    std::vector<EventType> seen;
    loop.add(sp.a, [&](int fd, EventType type) {
        EXPECT_EQ(fd, sp.a);
        seen.push_back(type);
        loop.stop();
    });
    ASSERT_EQ(::write(sp.b, "x", 1), 1);
    loop.run();
    EXPECT_EQ(seen, std::vector<EventType>{EventType::Read});
}

TEST(EventLoop, ModifySwitchesBetweenReadAndWriteInterest) {
    SocketPair sp;
    EventLoop loop;
    std::vector<EventType> seen;

    // Start with write interest only: an empty socket is writable at once.
    loop.add(sp.a, [&](int fd, EventType type) {
        seen.push_back(type);
        if (type == EventType::Write) {
            ASSERT_TRUE(loop.modify(fd, /*read=*/true, /*write=*/false));
            ASSERT_EQ(::write(sp.b, "x", 1), 1);
        } else {
            loop.stop();
        }
    }, /*read=*/false, /*write=*/true);

    loop.run();
    EXPECT_EQ(seen, (std::vector<EventType>{EventType::Write, EventType::Read}));
}

TEST(EventLoop, DisabledReadInterestDeliversNothing) {
    SocketPair sp;
    EventLoop loop;
    bool called = false;
    loop.add(sp.a, [&](int, EventType) { called = true; }, /*read=*/false, /*write=*/false);
    ASSERT_EQ(::write(sp.b, "x", 1), 1);

    int ticks = 0;
    loop.setTick(5ms, [&] {
        if (++ticks == 3) loop.stop();
    });
    loop.run();
    EXPECT_FALSE(called);
}

TEST(EventLoop, RemoveInsideOwnCallbackIsSafe) {
    // Regression test: remove() destroys the stored callback. run() must not be
    // executing that same std::function object when it is destroyed.
    // A use-after-free here is reported by AddressSanitizer.
    SocketPair sp;
    EventLoop loop;
    int calls = 0;
    std::vector<int> big_capture(1000, 7);  // Forces a heap-allocated std::function
    loop.add(sp.a, [&loop, &calls, big_capture](int fd, EventType) {
        loop.remove(fd);
        calls += big_capture[999];  // Touch captured state after remove()
        loop.stop();
    });
    ASSERT_EQ(::write(sp.b, "x", 1), 1);
    loop.run();
    EXPECT_EQ(calls, 7);
}

TEST(EventLoop, RemovedFdReceivesNoFurtherEvents) {
    SocketPair sp;
    EventLoop loop;
    int calls = 0;
    loop.add(sp.a, [&](int, EventType) { ++calls; });
    loop.remove(sp.a);
    ASSERT_EQ(::write(sp.b, "x", 1), 1);

    int ticks = 0;
    loop.setTick(5ms, [&] {
        if (++ticks == 3) loop.stop();
    });
    loop.run();
    EXPECT_EQ(calls, 0);
}

TEST(EventLoop, ModifyUnknownFdFails) {
    EventLoop loop;
    EXPECT_FALSE(loop.modify(12345, true, false));
}

TEST(EventLoop, AddInvalidFdThrows) {
    EventLoop loop;
    EXPECT_THROW(loop.add(-1, [](int, EventType) {}), std::system_error);
}
