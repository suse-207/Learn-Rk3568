#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>

namespace isoft {

// Minimal std::thread based replacement for the NAI-backed diag thread pool.
class ThreadPool
{
public:
    ThreadPool() = default;
    virtual ~ThreadPool() noexcept;

    ThreadPool(ThreadPool const&) = delete;
    ThreadPool& operator=(ThreadPool const&) = delete;

    static ThreadPool& GetInstance() noexcept;

    void Initialize(std::uint32_t const workerMax) noexcept;
    void Start() noexcept;
    void Stop() noexcept;

    template < class F, class... Args >
    bool Submit(F&& f, Args&&... args)
    {
        if (!running_.load()) {
            return false;
        }
        {
            std::lock_guard< std::mutex > lock{lock_};
            tasks_.emplace(std::bind(std::forward< F >(f), std::forward< Args >(args)...));
        }
        cond_.notify_one();
        return true;
    }

    std::uint32_t GetTaskNum();

private:
    void WorkerLoop() noexcept;

    std::atomic_bool running_{false};
    std::uint32_t workerMax_{4};
    std::mutex lock_;
    std::condition_variable cond_;
    std::queue< std::function< void() > > tasks_;
    std::vector< std::thread > workers_;
};

}  // namespace isoft
