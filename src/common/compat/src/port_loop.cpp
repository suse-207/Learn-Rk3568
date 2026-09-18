#include "port_loop.h"

#include <algorithm>
#include <cerrno>
#include <cstring>

#include <fcntl.h>
#include <unistd.h>

namespace portimpl {

static int64_t g_nowMs()
{
    using namespace std::chrono;
    return duration_cast< milliseconds >(steady_clock::now().time_since_epoch()).count();
}

TimerHandle::TimerHandle(PortLoop* loop) : loop_{loop} {}

TimerHandle::~TimerHandle()
{
    Cancel();
}

void TimerHandle::Start(int64_t const delayMs, std::function< void() > callback)
{
    callback_ = std::move(callback);
    if (delayMs < 0) {
        active_ = false;
        return;
    }
    Update(delayMs);
}

void TimerHandle::Update(int64_t const delayMs)
{
    deadlineMs_ = g_nowMs() + delayMs;
    active_     = true;
    if (loop_ != nullptr) {
        loop_->Wake();
    }
}

void TimerHandle::Cancel()
{
    active_ = false;
    if (loop_ != nullptr) {
        loop_->Wake();
    }
}

void TimerHandle::Invoke() noexcept
{
    if (active_.exchange(false)) {
        if (callback_) {
            callback_();
        }
    }
}

PortLoop::PortLoop()
{
    if (::pipe(wakePipe_) != 0) {
        wakePipe_[0] = -1;
        wakePipe_[1] = -1;
        return;
    }
    int flags = fcntl(wakePipe_[0], F_GETFL, 0);
    fcntl(wakePipe_[0], F_SETFL, flags | O_NONBLOCK);
    flags = fcntl(wakePipe_[1], F_GETFL, 0);
    fcntl(wakePipe_[1], F_SETFL, flags | O_NONBLOCK);
}

PortLoop::~PortLoop()
{
    Stop();
    if (wakePipe_[0] >= 0) {
        ::close(wakePipe_[0]);
    }
    if (wakePipe_[1] >= 0) {
        ::close(wakePipe_[1]);
    }
}

void PortLoop::StartThread()
{
    running_ = true;
    stop_    = false;
    thread_  = std::thread([this]() { RunCurrentThread(); });
}

void PortLoop::RunCurrentThread() noexcept
{
    loopThreadId_ = std::this_thread::get_id();
    while (!stop_.load()) {
        int timeoutMs = -1;
        std::vector< pollfd > pollFds;
        std::vector< FdItem > snapshot;
        std::vector< std::function< void() > > execSnapshot;
        std::vector< std::shared_ptr< TimerHandle > > timerSnapshot;

        {
            std::lock_guard< std::mutex > lock{mutex_};
            pollFds.reserve(fds_.size() + 1);
            pollFds.push_back(pollfd{wakePipe_[0], POLLIN, 0});
            for (auto const& entry : fds_) {
                if (entry.second.fd >= 0) {
                    pollFds.push_back(pollfd{entry.second.fd,
                                             static_cast< short >(entry.second.pollEvents), 0});
                    snapshot.push_back(entry.second);
                }
            }
            int64_t now = g_nowMs();
            timeoutMs   = -1;
            for (auto const& timer : timers_) {
                if (timer && timer->Active()) {
                    int64_t remain = timer->DeadlineMs() - now;
                    if (remain < 0) {
                        remain = 0;
                    }
                    if (timeoutMs < 0 || remain < timeoutMs) {
                        timeoutMs = static_cast< int >(remain);
                    }
                }
            }
            if (!execQueue_.empty()) {
                timeoutMs = 0;
            }
            while (!execQueue_.empty()) {
                execSnapshot.push_back(std::move(execQueue_.front()));
                execQueue_.pop();
            }
            timerSnapshot = timers_;
        }

        int pollResult = ::poll(pollFds.data(), pollFds.size(),
                                stop_.load() ? 0 : timeoutMs);
        if (pollResult < 0 && errno != EINTR) {
            continue;
        }

        if (!pollFds.empty() && (pollFds[0].revents & POLLIN)) {
            DrainWake();
        }

        std::vector< FdItem > readyItems;
        for (std::size_t i = 1; i < pollFds.size(); ++i) {
            if (pollFds[i].revents == 0) {
                continue;
            }
            auto const found = std::find_if(snapshot.begin(), snapshot.end(),
                                            [fd = pollFds[i].fd](FdItem const& item) {
                                                return item.fd == fd;
                                            });
            if (found != snapshot.end()) {
                readyItems.push_back(*found);
            }
        }

        for (auto const& exec : execSnapshot) {
            if (exec) {
                exec();
            }
        }

        for (auto const& item : readyItems) {
            uint32_t naiEvents = 0;
            bool active        = false;
            {
                std::lock_guard< std::mutex > lock{mutex_};
                auto it = fds_.find(item.fd);
                active  = (it != fds_.end() && it->second.callback);
            }
            if (!active) {
                continue;
            }
            // Events are level-triggered in the original NAI design. Only HUP is
            // translated to READ so the peer-close path can read() == 0.
            // Re-check the *current* fd events instead of stale snapshot revents.
            for (auto const& pfd : pollFds) {
                if (pfd.fd == item.fd) {
                    if (pfd.revents & (POLLIN | POLLRDHUP)) {
                        naiEvents |= 0x01u;  // NAI_EV_READ
                    }
                    if (pfd.revents & POLLOUT) {
                        naiEvents |= 0x02u;  // NAI_EV_WRITE
                    }
                    if (pfd.revents & POLLERR) {
                        naiEvents |= 0x10u;  // NAI_EV_ERROR
                    }
                    if (pfd.revents & POLLHUP) {
                        if ((pfd.revents & POLLIN) == 0) {
                            naiEvents |= 0x01u;
                        }
                        naiEvents |= 0x10u;
                    }
                    break;
                }
            }
            if (naiEvents != 0u && item.callback) {
                item.callback(naiEvents);
            }
        }

        if (timeoutMs == 0 || pollResult > 0 || !timerSnapshot.empty()) {
            std::vector< std::shared_ptr< TimerHandle > > due;
            int64_t now = g_nowMs();
            {
                std::lock_guard< std::mutex > lock{mutex_};
                due = timers_;
            }
            for (auto const& timer : due) {
                if (timer && timer->Active() && timer->DeadlineMs() <= now) {
                    timer->Invoke();
                }
            }
        }
    }
    running_ = false;
    DrainWake();
}

void PortLoop::Wake() noexcept
{
    if (wakePipe_[1] >= 0) {
        char byte = 1;
        ssize_t ignored = ::write(wakePipe_[1], &byte, 1);
        (void)ignored;
    }
}

void PortLoop::DrainWake() noexcept
{
    if (wakePipe_[0] >= 0) {
        char buffer[64];
        while (::read(wakePipe_[0], buffer, sizeof(buffer)) > 0) {
        }
    }
}

int64_t PortLoop::NowMs() const noexcept
{
    return g_nowMs();
}

void PortLoop::Stop() noexcept
{
    stop_ = true;
    Wake();
    if (thread_.joinable()) {
        thread_.join();
    }
}

bool PortLoop::AddFd(int fd, uint32_t const pollEvents, std::function< void(uint32_t) > callback)
{
    if (fd < 0) {
        return false;
    }
    {
        std::lock_guard< std::mutex > lock{mutex_};
        FdItem item;
        item.fd         = fd;
        item.pollEvents = pollEvents;
        item.callback   = std::move(callback);
        fds_[fd]        = std::move(item);
    }
    Wake();
    return true;
}

bool PortLoop::UpdateFd(int fd, std::function< void(uint32_t) > callback)
{
    if (fd < 0) {
        return false;
    }
    {
        std::lock_guard< std::mutex > lock{mutex_};
        auto it = fds_.find(fd);
        if (it == fds_.end()) {
            return false;
        }
        it->second.callback = std::move(callback);
    }
    Wake();
    return true;
}

bool PortLoop::DelFd(int fd)
{
    if (fd < 0) {
        return false;
    }
    {
        std::lock_guard< std::mutex > lock{mutex_};
        fds_.erase(fd);
    }
    Wake();
    return true;
}

bool PortLoop::Exec(std::function< void() > callback)
{
    {
        std::lock_guard< std::mutex > lock{mutex_};
        execQueue_.emplace(std::move(callback));
    }
    Wake();
    return true;
}

std::shared_ptr< TimerHandle > PortLoop::AddTimer(int64_t const delayMs,
                                                  std::function< void() > callback)
{
    auto timer          = std::make_shared< TimerHandle >(this);
    timer->seq_         = ++timerSeq_;
    {
        std::lock_guard< std::mutex > lock{mutex_};
        timers_.push_back(timer);
    }
    timer->Start(delayMs, std::move(callback));
    return timer;
}

bool PortLoop::InLoopThread() const noexcept
{
    return loopThreadId_ == std::this_thread::get_id();
}

}  // namespace portimpl
