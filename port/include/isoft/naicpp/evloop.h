#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <utility>

#include "isoft/naicpp/internal/port_fwd.h"

namespace isoft {
namespace naicpp {

class EvNodeTimer
{
public:
    EvNodeTimer() noexcept;
    ~EvNodeTimer();

    EvNodeTimer(EvNodeTimer const&) = delete;
    EvNodeTimer& operator=(EvNodeTimer const&) = delete;
    EvNodeTimer(EvNodeTimer&&) = delete;
    EvNodeTimer& operator=(EvNodeTimer&&) = delete;

    int UpdateTime(int msecond) noexcept;
    int Close() noexcept;

private:
    friend class EvLoop;
    explicit EvNodeTimer(std::shared_ptr<void> state) noexcept;
    std::shared_ptr<void> state_;
};

class EvLoop
{
public:
    using TimerPtr = std::shared_ptr< EvNodeTimer >;

    EvLoop(EvLoop const&) = delete;
    EvLoop& operator=(EvLoop const&) = delete;
    EvLoop(EvLoop&&) = delete;
    EvLoop& operator=(EvLoop&&) = delete;

    EvLoop() noexcept;
    ~EvLoop() noexcept;

    nai_evloop_t* GetRawEvLoop() const noexcept;

    int Open(bool const enableExecutor) noexcept;
    int Run(bool const useUserThread) noexcept;
    void Stop() noexcept;
    int Close() noexcept;

    int Exec(std::function< void() >&& fun) noexcept;
    int MakeTimer(TimerPtr& timer, int const msecond, std::function< void() >&& onTimerCallback) noexcept;

private:
    struct Impl;
    std::shared_ptr< Impl > impl_;
};

}  // namespace naicpp
}  // namespace isoft
