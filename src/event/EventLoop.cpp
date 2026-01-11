#include "EventLoop.hpp"

#include <unistd.h>

namespace event {

EventLoop::EventLoop() {
    kq_ = kqueue();
    if (kq_ < 0) {
        throw std::runtime_error("Failed to create kqueue");
    }
}

EventLoop::~EventLoop() {
    if (kq_ >= 0) {
        ::close(kq_);
    }
}

void EventLoop::addRead(int fd, EventCallback callback) {
    struct kevent ev{};
    EV_SET(&ev, fd, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, nullptr);

    if (kevent(kq_, &ev, 1, nullptr, 0, nullptr) < 0) {
        throw std::runtime_error("Failed to add read event");
    }

    read_callbacks_[fd] = std::move(callback);
}

void EventLoop::addWrite(int fd, EventCallback callback) {
    struct kevent ev{};
    EV_SET(&ev, fd, EVFILT_WRITE, EV_ADD | EV_ENABLE, 0, 0, nullptr);

    if (kevent(kq_, &ev, 1, nullptr, 0, nullptr) < 0) {
        throw std::runtime_error("Failed to add write event");
    }

    write_callbacks_[fd] = std::move(callback);
}

void EventLoop::disableWrite(int fd) {
    struct kevent ev{};
    EV_SET(&ev, fd, EVFILT_WRITE, EV_DISABLE, 0, 0, nullptr);
    kevent(kq_, &ev, 1, nullptr, 0, nullptr);  // Ignore errors
}

void EventLoop::remove(int fd) {
    struct kevent evs[2];
    EV_SET(&evs[0], fd, EVFILT_READ, EV_DELETE, 0, 0, nullptr);
    EV_SET(&evs[1], fd, EVFILT_WRITE, EV_DELETE, 0, 0, nullptr);

    // Ignore errors
    kevent(kq_, evs, 2, nullptr, 0, nullptr);

    read_callbacks_.erase(fd);
    write_callbacks_.erase(fd);
}

void EventLoop::run() {
    constexpr int MAX_EVENTS = 1024;  // Increased from 64 for better throughput
    struct kevent events[MAX_EVENTS];

    while (running_) {
        int n = kevent(kq_, nullptr, 0, events, MAX_EVENTS, nullptr);
        if (n < 0) {
            if (errno == EINTR) {
                continue; // Interrupted by signal, retry
            }
            throw std::runtime_error("kevent error");
        }

        for (int i = 0; i < n; ++i) {
            int fd = static_cast<int>(events[i].ident);

            if (events[i].filter == EVFILT_READ) {
                auto it = read_callbacks_.find(fd);
                if (it != read_callbacks_.end()) {
                    it->second(fd, EventType::Read);
                }
            }
            else if (events[i].filter == EVFILT_WRITE) {
                auto it = write_callbacks_.find(fd);
                if (it != write_callbacks_.end()) {
                    it->second(fd, EventType::Write);
                }
            }
        }
    }
}

void EventLoop::stop() {
    running_ = false;
}
} // namespace event
