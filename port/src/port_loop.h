#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

#include <poll.h>

namespace portimpl {

class PortLoop;

class TimerHandle final
{
public:
    explicit TimerHandle(PortLoop* loop);
    ~TimerHandle();

    TimerHandle(TimerHandle const&) = delete;
    TimerHandle& operator=(TimerHandle const&) = delete;

    void Start(int64_t delayMs, std::function< void() > callback);
    void Update(int64_t delayMs);
    void Cancel();

    bool Active() const noexcept { return active_; }
    int64_t DeadlineMs() const noexcept { return deadlineMs_; }
    uint64_t Seq() const noexcept { return seq_; }

    void Invoke() noexcept;

private:
    friend class PortLoop;

    PortLoop* loop_;
    std::function< void() > callback_;
    int64_t deadlineMs_{0};
    uint64_t seq_{0};
    std::atomic_bool active_{false};
};

class PortLoop final
{
public:
    PortLoop();
    ~PortLoop();

    PortLoop(PortLoop const&) = delete;
    PortLoop& operator=(PortLoop const&) = delete;

    void StartThread();
    void RunCurrentThread() noexcept;
    void Stop() noexcept;

    bool AddFd(int fd, uint32_t pollEvents, std::function< void(uint32_t) > callback);
    bool UpdateFd(int fd, std::function< void(uint32_t) > callback);
    bool DelFd(int fd);

    bool Exec(std::function< void() > callback);
    std::shared_ptr< TimerHandle > AddTimer(int64_t delayMs, std::function< void() > callback);

    bool InLoopThread() const noexcept;

private:
    friend class TimerHandle;

    void Wake() noexcept;
    void DrainWake() noexcept;
    int64_t NowMs() const noexcept;

    struct FdItem
    {
        int fd{-1};
        uint32_t pollEvents{0};
        std::function< void(uint32_t) > callback;
    };

    int wakePipe_[2]{-1, -1};
    std::atomic_bool running_{false};
    std::atomic_bool stop_{false};
    std::thread thread_;
    std::thread::id loopThreadId_;

    std::mutex mutex_;
    std::map< int, FdItem > fds_;
    std::queue< std::function< void() > > execQueue_;
    std::vector< std::shared_ptr< TimerHandle > > timers_;
    uint64_t timerSeq_{0};
};

}  // namespace portimpl
