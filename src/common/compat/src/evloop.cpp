#include "isoft/naicpp/evloop.h"

#include <atomic>
#include <cassert>
#include <memory>
#include <mutex>
#include <utility>

#include "ara/core/result.h"
#include "isoft/define.h"
#include "isoft/naicpp/global_evloop.h"
#include "port_loop.h"

namespace isoft {
namespace naicpp {

using portimpl::PortLoop;
using portimpl::TimerHandle;

struct EvLoop::Impl
{
    std::shared_ptr< PortLoop > loop{std::make_shared< PortLoop >()};
    std::atomic_bool opened{false};
    std::atomic_bool threadRunning{false};
};

EvNodeTimer::EvNodeTimer() noexcept = default;

EvNodeTimer::EvNodeTimer(std::shared_ptr< void > state) noexcept : state_{std::move(state)} {}

EvNodeTimer::~EvNodeTimer()
{
    Close();
}

int EvNodeTimer::UpdateTime(int const msecond) noexcept
{
    auto timer = std::static_pointer_cast< TimerHandle >(state_);
    if (!timer) {
        return -1;
    }
    if (msecond < 0) {
        timer->Cancel();
    } else {
        timer->Update(msecond);
    }
    return 0;
}

int EvNodeTimer::Close() noexcept
{
    auto timer = std::static_pointer_cast< TimerHandle >(state_);
    if (timer) {
        timer->Cancel();
    }
    state_.reset();
    return 0;
}

EvLoop::EvLoop() noexcept : impl_{std::make_shared< Impl >()} {}

EvLoop::~EvLoop() noexcept
{
    static_cast< void >(Close());
}

nai_evloop_t* EvLoop::GetRawEvLoop() const noexcept
{
    return reinterpret_cast< nai_evloop_t* >(impl_->loop.get());
}

int EvLoop::Open(bool const enableExecutor) noexcept
{
    (void)enableExecutor;
    impl_->opened = true;
    return isoft::kSuccess;
}

int EvLoop::Run(bool const useUserThread) noexcept
{
    if (useUserThread) {
        impl_->loop->RunCurrentThread();
        return isoft::kSuccess;
    }
    impl_->loop->StartThread();
    impl_->threadRunning = true;
    return isoft::kSuccess;
}

void EvLoop::Stop() noexcept
{
    impl_->loop->Stop();
}

int EvLoop::Close() noexcept
{
    impl_->loop->Stop();
    impl_->threadRunning = false;
    return isoft::kSuccess;
}

int EvLoop::Exec(std::function< void() >&& fun) noexcept
{
    if (!impl_ || !fun) {
        return -1;
    }
    return impl_->loop->Exec(std::move(fun)) ? isoft::kSuccess : isoft::kFailure;
}

int EvLoop::MakeTimer(TimerPtr& timer, int const msecond, std::function< void() >&& onTimerCallback) noexcept
{
    if (!impl_ || !onTimerCallback) {
        return -1;
    }

    auto state = impl_->loop->AddTimer(msecond, std::move(onTimerCallback));
    if (!state) {
        return -1;
    }
    std::shared_ptr< EvNodeTimer > node{new EvNodeTimer(std::move(state))};
    timer = std::move(node);
    return isoft::kSuccess;
}

namespace {

std::mutex gGlobalLoopLock;
std::shared_ptr< EvLoop > gGlobalLoop;
GlobalGeneralEvLoop::Mode gGlobalMode{GlobalGeneralEvLoop::Mode::kInsideThread};
bool gGlobalEnabledExecutor{true};

}  // namespace

ara::core::Result< void > GlobalGeneralEvLoop::Initialize(Mode const runMode, bool const enableExecutor) noexcept
{
    std::lock_guard< std::mutex > lock{gGlobalLoopLock};
    gGlobalMode           = runMode;
    gGlobalEnabledExecutor = enableExecutor;
    return {};
}

void GlobalGeneralEvLoop::Deinitialize() noexcept
{
    std::lock_guard< std::mutex > lock{gGlobalLoopLock};
    if (gGlobalLoop) {
        gGlobalLoop->Close();
        gGlobalLoop.reset();
    }
}

std::shared_ptr< EvLoop > GlobalGeneralEvLoop::Get() noexcept
{
    std::lock_guard< std::mutex > lock{gGlobalLoopLock};
    if (gGlobalLoop) {
        return gGlobalLoop;
    }
    gGlobalLoop = std::make_shared< EvLoop >();
    if (!gGlobalLoop) {
        return nullptr;
    }
    static_cast< void >(gGlobalLoop->Open(gGlobalEnabledExecutor));
    if (gGlobalMode == Mode::kInsideThread) {
        static_cast< void >(gGlobalLoop->Run(false));
    }
    return gGlobalLoop;
}

}  // namespace naicpp
}  // namespace isoft
