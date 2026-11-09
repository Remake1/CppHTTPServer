#ifndef CPPHTTPSERVER_EVENTLOOP_HPP
#define CPPHTTPSERVER_EVENTLOOP_HPP


#include <sys/event.h>
#include <atomic>
#include <chrono>
#include <functional>
#include <unordered_map>


namespace event {
enum class EventType {
    Read,
    Write,
};

using EventCallback = std::function<void(int fd, EventType type)>;
using TickCallback = std::function<void()>;

// Level-triggered kqueue loop. Each fd has one callback that receives both its
// read and write events; which of the two are delivered is set by modify().
class EventLoop {
public:
    EventLoop();
    ~EventLoop();

    EventLoop(const EventLoop&) = delete;
    EventLoop& operator=(const EventLoop&) = delete;

    // Registers fd. Throws std::system_error on failure.
    void add(int fd, EventCallback callback, bool read = true, bool write = false);

    // Changes which events fd receives. Makes no syscall if nothing changed.
    // Returns false on failure.
    bool modify(int fd, bool read, bool write);

    // Unregisters fd. Safe to call from inside fd's own callback.
    void remove(int fd);

    // Calls callback every interval (one tick timer per loop).
    void setTick(std::chrono::milliseconds interval, TickCallback callback);

    void run();

    // Thread-safe: wakes the loop, which returns from run() promptly.
    void stop();
private:
    struct Watch {
        EventCallback callback;
        bool read;
        bool write;
    };

    int kq_;  // kqueue file descriptor
    std::atomic<bool> running_{true};
    std::unordered_map<int, Watch> watches_;
    TickCallback tick_;
};
} // namespace event


#endif //CPPHTTPSERVER_EVENTLOOP_HPP
