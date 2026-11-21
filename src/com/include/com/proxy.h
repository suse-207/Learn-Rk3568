// Copyright 2024
// Licensed under the Apache License, Version 2.0

/// @file       proxy.h
/// @brief      Proxy layer - client side service access
/// @details    Contains BindHandle, BindProxy, and ServiceProxy template

#pragma once

#include "com/types.h"
#include "com/runtime.h"
#include <memory>
#include <functional>
#include <vector>
#include <map>
#include <string>
#include <utility>
#include <mutex>
#include <thread>
#include <chrono>
#include <condition_variable>

namespace com
{
    namespace proxy
    {

        /// @brief Binding layer handle abstract interface
        /// @details Represents a connection to a specific service instance.
        ///          Each protocol implementation must provide its own BindHandle.
        class BindHandle
        {
        public:
            BindHandle() noexcept = default;
            virtual ~BindHandle() noexcept = default;

            BindHandle(BindHandle const &) = delete;
            BindHandle &operator=(BindHandle const &) = delete;

            /// @brief Check if handle is valid
            virtual bool IsValid() const noexcept = 0;

            /// @brief Get the binding runtime name
            virtual char const *GetBindRuntimeName() const noexcept = 0;

            /// @brief Send a request and wait for the response
            virtual Result<std::vector<uint8_t>> SendRequest(
                MethodIdentifier methodId,
                std::vector<uint8_t> const &requestData,
                Duration timeout) noexcept = 0;

            /// @brief Send a request asynchronously, returning a Future (P2: ara::com-style)
            /// @details 不承诺超时：只在响应到达或 Stop() 时结算（com 不拥有 loop，
            ///          没有线程能推进 deadline）
            virtual Future<std::vector<uint8_t>> SendRequestAsync(
                MethodIdentifier methodId,
                std::vector<uint8_t> const &requestData) noexcept = 0;

            /// @brief Subscribe to an event group and register a notification handler
            /// @param[in] eventId Event identifier
            /// @param[in] eventGroupId Event group the event belongs to
            /// @param[in] handler Called with the serialized event payload
            virtual void SubscribeEvent(
                EventIdentifier eventId,
                EventGroupIdentifier eventGroupId,
                std::function<void(std::vector<uint8_t> const &)> handler) noexcept = 0;

            /// @brief Unsubscribe from an event group
            /// @param[in] eventId Event identifier
            /// @param[in] eventGroupId Event group the event belongs to
            virtual void UnsubscribeEvent(
                EventIdentifier eventId,
                EventGroupIdentifier eventGroupId) noexcept = 0;
        };

        /// @brief Binding layer proxy abstract interface
        /// @details Base class for protocol-specific proxy implementations.
        class BindProxy
        {
        public:
            BindProxy() noexcept = default;
            virtual ~BindProxy() noexcept = default;

            BindProxy(BindProxy const &) = delete;
            BindProxy &operator=(BindProxy const &) = delete;

            /// @brief Initialize the proxy
            virtual Result<void> Init() noexcept = 0;

            /// @brief Deinitialize the proxy
            virtual Result<void> Deinit() noexcept = 0;

            /// @brief Get the binding runtime name
            virtual char const *GetBindRuntimeName() const noexcept = 0;
        };

        /// @brief Service proxy class
        /// @details Application layer uses this to access remote services.
        ///          P3: Optionally accepts an InstanceSpecifier for AUTOSAR-style identification.
        class ServiceProxy
        {
        public:
            /// @brief Constructor with numeric IDs
            ServiceProxy(
                ServiceIdentifier const &serviceIdentifier,
                InstanceIdentifier const &instanceIdentifier) noexcept
                : serviceIdentifier_(serviceIdentifier),
                  instanceIdentifier_(instanceIdentifier),
                  instanceSpecifier_{"unknown"}
            {
            }

            /// @brief Constructor with InstanceSpecifier (P3)
            ServiceProxy(
                InstanceSpecifier const &specifier,
                ServiceIdentifier const &serviceIdentifier,
                InstanceIdentifier const &instanceIdentifier) noexcept
                : serviceIdentifier_(serviceIdentifier),
                  instanceIdentifier_(instanceIdentifier),
                  instanceSpecifier_(specifier)
            {
            }

            ~ServiceProxy()
            {
                Deinit();
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

            /// @brief 等待服务可用（事件驱动，无轮询）
            bool WaitForAvailable(Duration timeout) noexcept
            {
                std::unique_lock<std::mutex> stateLock(state_->mutex);
                return state_->changed.wait_for(
                    stateLock, timeout, [this]() { return state_->availableBindings > 0; });
            }

            /// @brief Get instance specifier (P3)
            InstanceSpecifier const &GetInstanceSpecifier() const noexcept
            {
                return instanceSpecifier_;
            }

        protected:
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

            ServiceIdentifier serviceIdentifier_;
            InstanceIdentifier instanceIdentifier_;
            InstanceSpecifier instanceSpecifier_;
            std::shared_ptr<DiscoveryState> state_{std::make_shared<DiscoveryState>()};
            std::vector<std::pair<std::string, FindServiceHandle>> registrations_;
            mutable std::mutex mutex_;
            bool initialized_{false};
        };

    } // namespace proxy
} // namespace com
