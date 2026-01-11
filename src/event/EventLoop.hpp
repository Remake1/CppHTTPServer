#ifndef CPPHTTPSERVER_EVENTLOOP_HPP
#define CPPHTTPSERVER_EVENTLOOP_HPP


#include <sys/event.h>
#include <functional>
#include <unordered_map>


namespace event {
enum class EventType {
    Read,
    Write,
};

using EventCallback = std::function<void(int fd, EventType type)>;

class EventLoop {
public:
    EventLoop();
    ~EventLoop();

    EventLoop(const EventLoop&) = delete;
    EventLoop& operator=(const EventLoop&) = delete;

    void addRead(int fd, EventCallback callback);
    void addWrite(int fd, EventCallback callback);
    void disableWrite(int fd);  // Disable write events while keeping read active
    void modify(int fd, bool read, bool write);
    void remove(int fd);

    void run();
    void stop();
private:
    int kq_;  // kqueue file descriptor
    bool running_ = true;
    std::unordered_map<int, EventCallback> read_callbacks_;
    std::unordered_map<int, EventCallback> write_callbacks_;
};
} // namespace event


#endif //CPPHTTPSERVER_EVENTLOOP_HPP