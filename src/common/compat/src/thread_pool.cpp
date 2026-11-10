#include "thread_pool/thread_pool.h"

#include <chrono>
#include <utility>
#include <vector>

namespace isoft {

ThreadPool::~ThreadPool() noexcept
{
    Stop();
}

ThreadPool& ThreadPool::GetInstance() noexcept
{
    static ThreadPool instance;
    return instance;
}

void ThreadPool::Initialize(std::uint32_t const workerMax) noexcept
{
    workerMax_ = workerMax;
    if (workerMax_ == 0) {
        workerMax_ = 1;
    }
}

void ThreadPool::Start() noexcept
{
    if (running_.exchange(true)) {
        return;
    }
    std::lock_guard< std::mutex > lock{lock_};
    workers_.clear();
    for (std::uint32_t i = 0; i < workerMax_; ++i) {
        workers_.emplace_back([this]() { WorkerLoop(); });
    }
}

void ThreadPool::Stop() noexcept
{
    // 先在持锁状态下置位，再唤醒；join 必须在释放 lock_ 之后进行。
    // 否则主线程持锁 join、被唤起的 worker 又在 cond_.wait_for 内抢同一把
    // 锁才能返回，双方互等，进程卡死在 Stop()（Ctrl-C 无法退出）。
    {
        std::lock_guard< std::mutex > lock{lock_};
        running_ = false;
    }
    cond_.notify_all();
    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    workers_.clear();
}

std::uint32_t ThreadPool::GetTaskNum()
{
    std::lock_guard< std::mutex > lock{lock_};
    return static_cast< std::uint32_t >(tasks_.size());
}

void ThreadPool::WorkerLoop() noexcept
{
    for (;;) {
        std::function< void() > task;
        {
            std::unique_lock< std::mutex > lock{lock_};
            cond_.wait_for(lock, std::chrono::milliseconds(200), [this]() {
                return !running_.load() || !tasks_.empty();
            });
            if (!running_.load()) {
                return;
            }
            if (tasks_.empty()) {
                continue;
            }
            task = std::move(tasks_.front());
            tasks_.pop();
        }
        if (task) {
            task();
        }
    }
}

}  // namespace isoft
