#include "EventLoop.hpp"

#include <cerrno>
#include <system_error>
#include <unistd.h>

namespace event {

namespace {
// Identifiers for the non-fd events. kqueue keys events by (ident, filter),
// so these can't collide with fd watches.
constexpr uintptr_t kWakeIdent = 0;
constexpr uintptr_t kTickIdent = 0;

uint16_t enableFlag(bool on) {
    return EV_ADD | (on ? EV_ENABLE : EV_DISABLE);
}
} // namespace

EventLoop::EventLoop() {
    kq_ = kqueue();
    if (kq_ < 0) {
        throw std::system_error(errno, std::generic_category(), "kqueue");
    }

    // User event that stop() triggers to wake a blocked kevent() call.
    struct kevent ev{};
    EV_SET(&ev, kWakeIdent, EVFILT_USER, EV_ADD | EV_CLEAR, 0, 0, nullptr);
    if (kevent(kq_, &ev, 1, nullptr, 0, nullptr) < 0) {
        int err = errno;
        ::close(kq_);
        throw std::system_error(err, std::generic_category(), "kevent(EVFILT_USER)");
    }
}

EventLoop::~EventLoop() {
    if (kq_ >= 0) {
        ::close(kq_);
    }
}

void EventLoop::add(int fd, EventCallback callback, bool read, bool write) {
    // Register both filters up front (disabled ones cost nothing), so later
    // interest changes are a cheap EV_ENABLE/EV_DISABLE.
    struct kevent evs[2];
    EV_SET(&evs[0], fd, EVFILT_READ, enableFlag(read), 0, 0, nullptr);
    EV_SET(&evs[1], fd, EVFILT_WRITE, enableFlag(write), 0, 0, nullptr);
    if (kevent(kq_, evs, 2, nullptr, 0, nullptr) < 0) {
        throw std::system_error(errno, std::generic_category(), "kevent(add)");
    }
    watches_[fd] = Watch{std::move(callback), read, write};
}

bool EventLoop::modify(int fd, bool read, bool write) {
    auto it = watches_.find(fd);
    if (it == watches_.end()) {
        return false;
    }
    Watch& w = it->second;

    struct kevent evs[2];
    int n = 0;
    if (w.read != read) {
        EV_SET(&evs[n++], fd, EVFILT_READ, enableFlag(read), 0, 0, nullptr);
    }
    if (w.write != write) {
        EV_SET(&evs[n++], fd, EVFILT_WRITE, enableFlag(write), 0, 0, nullptr);
    }
    if (n > 0 && kevent(kq_, evs, n, nullptr, 0, nullptr) < 0) {
        return false;
    }
    w.read = read;
    w.write = write;
    return true;
}

void EventLoop::remove(int fd) {
    struct kevent evs[2];
    EV_SET(&evs[0], fd, EVFILT_READ, EV_DELETE, 0, 0, nullptr);
    EV_SET(&evs[1], fd, EVFILT_WRITE, EV_DELETE, 0, 0, nullptr);

    // Ignore errors: closing the fd removes its filters anyway.
    kevent(kq_, evs, 2, nullptr, 0, nullptr);

    watches_.erase(fd);
}

void EventLoop::setTick(std::chrono::milliseconds interval, TickCallback callback) {
    struct kevent ev{};
    // EVFILT_TIMER's data is in milliseconds by default.
    EV_SET(&ev, kTickIdent, EVFILT_TIMER, EV_ADD | EV_ENABLE, 0, interval.count(), nullptr);
    if (kevent(kq_, &ev, 1, nullptr, 0, nullptr) < 0) {
        throw std::system_error(errno, std::generic_category(), "kevent(EVFILT_TIMER)");
    }
    tick_ = std::move(callback);
}

void EventLoop::run() {
    constexpr int MAX_EVENTS = 1024;
    struct kevent events[MAX_EVENTS];

    while (running_) {
        int n = kevent(kq_, nullptr, 0, events, MAX_EVENTS, nullptr);
        if (n < 0) {
            if (errno == EINTR) {
                continue; // Interrupted by signal, retry
            }
            throw std::system_error(errno, std::generic_category(), "kevent(wait)");
        }

        for (int i = 0; i < n && running_; ++i) {
            const struct kevent& ev = events[i];

            if (ev.filter == EVFILT_USER) {
                continue;  // Wake-up from stop(); the while condition handles it
            }
            if (ev.filter == EVFILT_TIMER) {
                if (tick_) tick_();
                continue;
            }

            const int fd = static_cast<int>(ev.ident);
            auto it = watches_.find(fd);
            if (it == watches_.end()) {
                continue;  // Removed earlier in this batch
            }

            // An earlier callback in this batch may have changed the interest
            // set (or closed the fd and reused its number), leaving this event
            // stale. Drop events the fd is no longer interested in.
            const bool is_read = (ev.filter == EVFILT_READ);
            if (is_read ? !it->second.read : !it->second.write) {
                continue;
            }

            // Invoke a copy: the callback may remove() this fd, which would
            // destroy the stored std::function while it is still executing.
            EventCallback callback = it->second.callback;
            callback(fd, is_read ? EventType::Read : EventType::Write);
        }
    }
}

void EventLoop::stop() {
    running_ = false;

    struct kevent ev{};
    EV_SET(&ev, kWakeIdent, EVFILT_USER, 0, NOTE_TRIGGER, 0, nullptr);
    kevent(kq_, &ev, 1, nullptr, 0, nullptr);
}
} // namespace event
