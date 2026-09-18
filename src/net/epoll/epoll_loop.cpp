#include "net/epoll/epoll_loop.h"

#include <sys/eventfd.h>
#include <unistd.h>

#include <cerrno>
#include <utility>
#include <vector>

EpollLoop::~EpollLoop() {
    if (wake_fd_ >= 0) {
        ::close(wake_fd_);
    }
    if (epfd_ >= 0) {
        ::close(epfd_);
    }
}

bool EpollLoop::init(int max_events) {
    max_events_ = max_events;
    epfd_ = ::epoll_create1(0);
    if (epfd_ < 0) {
        return false;
    }

    wake_fd_ = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (wake_fd_ < 0) {
        ::close(epfd_);
        epfd_ = -1;
        return false;
    }
    return add(wake_fd_, EPOLLIN, [this](std::uint32_t events) { on_wake(events); });
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

bool EpollLoop::post(std::function<void()> task) {
    {
        std::lock_guard<std::mutex> lock(post_mutex_);
        pending_.push(std::move(task));
    }
    const std::uint64_t value = 1;
    const ssize_t ignored = ::write(wake_fd_, &value, sizeof(value));
    (void)ignored;
    return true;
}

void EpollLoop::on_wake(std::uint32_t) {
    std::uint64_t value = 0;
    while (::read(wake_fd_, &value, sizeof(value)) > 0) {
    }

    std::queue<std::function<void()>> tasks;
    {
        std::lock_guard<std::mutex> lock(post_mutex_);
        tasks.swap(pending_);
    }

    while (!tasks.empty()) {
        auto task = std::move(tasks.front());
        tasks.pop();
        task();
    }
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
