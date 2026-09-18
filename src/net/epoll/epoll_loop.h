#pragma once

#include <sys/epoll.h>

#include <cstdint>
#include <functional>
#include <mutex>
#include <queue>
#include <unordered_map>

class EpollLoop {
public:
    ~EpollLoop();

    bool init(int max_events);
    void run();
    void stop();

    bool add(int fd, std::uint32_t events, std::function<void(std::uint32_t)> cb);
    bool mod(int fd, std::uint32_t events);
    bool del(int fd);
    bool post(std::function<void()> task);

private:
    void on_wake(std::uint32_t events);

    int epfd_ = -1;
    int wake_fd_ = -1;
    int max_events_ = 1024;
    bool running_ = false;
    std::unordered_map<int, std::function<void(std::uint32_t)>> callbacks_;
    std::mutex post_mutex_;
    std::queue<std::function<void()>> pending_;
};
