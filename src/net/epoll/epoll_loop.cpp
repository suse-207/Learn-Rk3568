#include "net/epoll/epoll_loop.h"

#include <unistd.h>

#include <cerrno>
#include <vector>

EpollLoop::~EpollLoop() {
    if (epfd_ >= 0) {
        ::close(epfd_);
    }
}

bool EpollLoop::init(int max_events) {
    max_events_ = max_events;
    epfd_ = ::epoll_create1(0);
    return epfd_ >= 0;
}

bool EpollLoop::add(int fd, std::uint32_t events, std::function<void(std::uint32_t)> cb) {
    struct epoll_event ev {};
    ev.events = events;
    ev.data.fd = fd;
    if (::epoll_ctl(epfd_, EPOLL_CTL_ADD, fd, &ev) != 0) {
        return false;
    }
    callbacks_[fd] = std::move(cb);
    return true;
}

bool EpollLoop::mod(int fd, std::uint32_t events) {
    struct epoll_event ev {};
    ev.events = events;
    ev.data.fd = fd;
    return ::epoll_ctl(epfd_, EPOLL_CTL_MOD, fd, &ev) == 0;
}

bool EpollLoop::del(int fd) {
    ::epoll_ctl(epfd_, EPOLL_CTL_DEL, fd, nullptr);
    callbacks_.erase(fd);
    return true;
}

void EpollLoop::run() {
    running_ = true;
    std::vector<struct epoll_event> events(static_cast<std::size_t>(max_events_));

    while (running_) {
        const int n = ::epoll_wait(epfd_, events.data(), static_cast<int>(events.size()), 500);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        for (int i = 0; i < n; ++i) {
            const int fd = events[i].data.fd;
            auto it = callbacks_.find(fd);
            if (it != callbacks_.end()) {
                it->second(events[i].events);
            }
        }
    }
}

void EpollLoop::stop() {
    running_ = false;
}
