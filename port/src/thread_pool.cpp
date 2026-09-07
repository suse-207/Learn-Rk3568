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
    running_ = false;
    cond_.notify_all();
    std::lock_guard< std::mutex > lock{lock_};
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
