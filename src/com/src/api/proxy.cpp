// Copyright 2024
// Licensed under the Apache License, Version 2.0

/// @file       proxy.cpp
/// @brief      ServiceProxy 实现（pimpl）
/// @details    发现状态、绑定句柄、回调注册全部只存在于本文件，公共头看不到它们。

#include "com/api/proxy.h"

#include "com/api/runtime.h"
#include "com/binding/bind_handle.h"
#include "com/binding/bind_runtime.h"

#include <condition_variable>
#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace com
{
    namespace proxy
    {

        /// @brief 发现状态：与 proxy 生命周期解耦，回调只持有 weak_ptr
        struct DiscoveryState
        {
            std::mutex mutex;
            std::condition_variable changed;
            std::map<std::string, ServiceHandleContainer<std::shared_ptr<BindHandle>>> handles;
            std::size_t availableBindings{0};
            bool active{false};

            /// @brief 重算可用绑定数并唤醒等待者（调用方必须持 mutex）
            void RecomputeLocked() noexcept
            {
                std::size_t available = 0;
                for (auto const &entry : handles)
                {
                    if (!entry.second.empty())
                    {
                        ++available;
                    }
                }
                availableBindings = available;
                changed.notify_all();
            }
        };

        /// @brief ServiceProxy 的内部状态
        class ServiceProxy::Impl
        {
        public:
            Impl(ServiceIdentifier const &serviceIdentifier,
                 InstanceIdentifier const &instanceIdentifier,
                 InstanceSpecifier const &specifier) noexcept
                : serviceIdentifier_(serviceIdentifier),
                  instanceIdentifier_(instanceIdentifier),
                  instanceSpecifier_(specifier)
            {
            }

            Result<void> Init() noexcept
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (initialized_)
                {
                    return Result<void>::FromValue();
                }

                auto *runtime = Runtime::Get();
                if (!runtime)
                {
                    return Result<void>::FromError(ComErrc::kNotInitialized);
                }

                {
                    std::lock_guard<std::mutex> stateLock(state_->mutex);
                    state_->active = true;
                }

                for (auto const &name : runtime->GetBindRuntimeNames())
                {
                    auto *bindRuntime = runtime->GetBindRuntime(name);
                    if (!bindRuntime)
                    {
                        continue;
                    }

                    // 1) 先取一次快照：服务已经在线时，不必等回调
                    ServiceHandleContainer<std::shared_ptr<BindHandle>> snapshot;
                    bindRuntime->GetAvailableServiceHandles(
                        serviceIdentifier_, instanceIdentifier_, snapshot);
                    UpdateHandles(name, snapshot);

                    // 2) 再注册发现回调：之后的上线/下线都走事件，不再轮询
                    FindServiceHandle findHandle{serviceIdentifier_, instanceIdentifier_};
                    std::weak_ptr<DiscoveryState> weakState = state_;
                    bindRuntime->RegisterFindServiceHandle(
                        findHandle,
                        [weakState, name](
                            ServiceHandleContainer<std::shared_ptr<BindHandle>> const &handles)
                        {
                            auto state = weakState.lock();
                            if (!state)
                            {
                                return;
                            }
                            std::lock_guard<std::mutex> stateLock(state->mutex);
                            if (!state->active)
                            {
                                return;
                            }
                            ServiceHandleContainer<std::shared_ptr<BindHandle>> filtered;
                            for (auto const &handle : handles)
                            {
                                if (handle)
                                {
                                    filtered.push_back(handle);
                                }
                            }
                            if (filtered.empty())
                            {
                                state->handles.erase(name);
                            }
                            else
                            {
                                state->handles[name] = std::move(filtered);
                            }
                            state->RecomputeLocked();
                        });
                    registrations_.emplace_back(name, findHandle);
                }

                initialized_ = true;
                return Result<void>::FromValue();
            }

            Result<void> Deinit() noexcept
            {
                std::vector<std::pair<std::string, FindServiceHandle>> registrations;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    if (!initialized_)
                    {
                        return Result<void>::FromValue();
                    }
                    registrations.swap(registrations_);
                    initialized_ = false;
                    std::lock_guard<std::mutex> stateLock(state_->mutex);
                    state_->active = false;
                    state_->handles.clear();
                    state_->RecomputeLocked();
                }

                // 注销放在锁外：vsomeip 可能在注销时同步回调
                if (auto *runtime = Runtime::Get())
                {
                    for (auto const &entry : registrations)
                    {
                        if (auto *bindRuntime = runtime->GetBindRuntime(entry.first))
                        {
                            bindRuntime->UnregisterFindServiceHandle(entry.second);
                        }
                    }
                }

                return Result<void>::FromValue();
            }

            bool IsAvailable() const noexcept
            {
                std::lock_guard<std::mutex> stateLock(state_->mutex);
                return state_->availableBindings > 0;
            }

            bool WaitForAvailable(Duration timeout) noexcept
            {
                std::unique_lock<std::mutex> stateLock(state_->mutex);
                return state_->changed.wait_for(
                    stateLock, timeout, [this]() { return state_->availableBindings > 0; });
            }

            InstanceSpecifier const &GetInstanceSpecifier() const noexcept
            {
                return instanceSpecifier_;
            }

            /// @brief 把一次句柄快照写入发现状态（Init 里的首次快照）
            void UpdateHandles(
                std::string const &bindName,
                ServiceHandleContainer<std::shared_ptr<BindHandle>> const &handles) noexcept
            {
                std::lock_guard<std::mutex> stateLock(state_->mutex);
                ServiceHandleContainer<std::shared_ptr<BindHandle>> filtered;
                for (auto const &handle : handles)
                {
                    if (handle)
                    {
                        filtered.push_back(handle);
                    }
                }
                if (filtered.empty())
                {
                    state_->handles.erase(bindName);
                }
                else
                {
                    state_->handles[bindName] = std::move(filtered);
                }
                state_->RecomputeLocked();
            }

        private:
            ServiceIdentifier serviceIdentifier_;
            InstanceIdentifier instanceIdentifier_;
            InstanceSpecifier instanceSpecifier_;
            std::shared_ptr<DiscoveryState> state_{std::make_shared<DiscoveryState>()};
            std::vector<std::pair<std::string, FindServiceHandle>> registrations_;
            mutable std::mutex mutex_;
            bool initialized_{false};
        };

        // -------------------------------------------------------------------
        // 公共转发（行为与 pimpl 之前完全一致）
        // -------------------------------------------------------------------

        ServiceProxy::ServiceProxy(
            ServiceIdentifier const &serviceIdentifier,
            InstanceIdentifier const &instanceIdentifier) noexcept
            : impl_(std::make_unique<Impl>(
                  serviceIdentifier, instanceIdentifier, InstanceSpecifier{"unknown"}))
        {
        }

        ServiceProxy::ServiceProxy(
            InstanceSpecifier const &specifier,
            ServiceIdentifier const &serviceIdentifier,
            InstanceIdentifier const &instanceIdentifier) noexcept
            : impl_(std::make_unique<Impl>(serviceIdentifier, instanceIdentifier, specifier))
        {
        }

        ServiceProxy::~ServiceProxy()
        {
            Deinit();
        }

        Result<void> ServiceProxy::Init() noexcept
        {
            return impl_->Init();
        }

        Result<void> ServiceProxy::Deinit() noexcept
        {
            if (!impl_)
            {
                return Result<void>::FromValue();
            }
            return impl_->Deinit();
        }

        bool ServiceProxy::IsAvailable() const noexcept
        {
            return impl_->IsAvailable();
        }

        bool ServiceProxy::WaitForAvailable(Duration timeout) noexcept
        {
            return impl_->WaitForAvailable(timeout);
        }

        InstanceSpecifier const &ServiceProxy::GetInstanceSpecifier() const noexcept
        {
            return impl_->GetInstanceSpecifier();
        }

    } // namespace proxy
} // namespace com
