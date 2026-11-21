// Copyright 2024
// Licensed under the Apache License, Version 2.0

/// @file       proxy.cpp
/// @brief      HandleType 与发现面实现
/// @details    绑定向导、发现注册、在途请求全部只在本文件可见。

#include "com/api/proxy.h"

#include "com/api/runtime.h"
#include "com/binding/bind_handle.h"
#include "com/binding/bind_runtime.h"
#include "com/binding/bind_runtime_registry.h"

#include <map>
#include <mutex>
#include <utility>
#include <vector>

namespace com
{
    namespace proxy
    {

        // ------------------------------------------------------------------
        // 句柄实现
        // ------------------------------------------------------------------

        class detail::HandleImpl
        {
        public:
            explicit HandleImpl(std::shared_ptr<BindHandle> handle) noexcept
                : handle_(std::move(handle)),
                  instance_(handle_ ? handle_->GetInstanceId() : InstanceIdentifier{0})
            {
            }

            std::shared_ptr<BindHandle> handle_;
            InstanceIdentifier instance_{0};
        };

        // ------------------------------------------------------------------
        // HandleType
        // ------------------------------------------------------------------

        HandleType::HandleType() noexcept = default;
        HandleType::~HandleType() noexcept = default;
        HandleType::HandleType(HandleType const &) noexcept = default;
        HandleType &HandleType::operator=(HandleType const &) noexcept = default;
        HandleType::HandleType(HandleType &&) noexcept = default;
        HandleType &HandleType::operator=(HandleType &&) noexcept = default;

        HandleType::HandleType(std::shared_ptr<detail::HandleImpl> impl) noexcept
            : impl_(std::move(impl))
        {
        }

        HandleType::operator bool() const noexcept
        {
            return IsValid();
        }

        bool HandleType::IsValid() const noexcept
        {
            return impl_ && impl_->handle_ && impl_->handle_->IsValid();
        }

        InstanceIdentifier const &HandleType::GetInstanceId() const noexcept
        {
            static const InstanceIdentifier kNone{0};
            return impl_ ? impl_->instance_ : kNone;
        }

        bool HandleType::operator==(HandleType const &other) const noexcept
        {
            return GetInstanceId() == other.GetInstanceId();
        }

        bool HandleType::operator<(HandleType const &other) const noexcept
        {
            return GetInstanceId() < other.GetInstanceId();
        }

        HandleType detail::HandleAccess::Make(std::shared_ptr<HandleImpl> impl) noexcept
        {
            return HandleType{std::move(impl)};
        }

        std::shared_ptr<detail::HandleImpl> detail::HandleAccess::Get(HandleType const &handle) noexcept
        {
            return handle.impl_;
        }

        // ------------------------------------------------------------------
        // 发现
        // ------------------------------------------------------------------

        namespace
        {
            /// @brief 把绑定层句柄快照转成公共句柄列表
            ServiceHandleContainer<HandleType> Convert(
                ServiceHandleContainer<std::shared_ptr<BindHandle>> const &bindHandles)
            {
                ServiceHandleContainer<HandleType> out;
                out.reserve(bindHandles.size());
                for (auto const &bindHandle : bindHandles)
                {
                    if (!bindHandle)
                    {
                        continue;
                    }
                    out.push_back(detail::HandleAccess::Make(
                        std::make_shared<detail::HandleImpl>(bindHandle)));
                }
                return out;
            }

            /// @brief 一路异步发现的注册记录
            struct DiscoveryRegistration
            {
                ServiceIdentifier serviceId{0};
                std::vector<std::pair<std::string, FindServiceHandle>> registrations;
            };

            struct DiscoveryRegistry
            {
                std::mutex mutex;
                std::map<std::uint64_t, DiscoveryRegistration> entries;

                static DiscoveryRegistry &Instance()
                {
                    static DiscoveryRegistry registry;
                    return registry;
                }
            };
        } // namespace

        namespace detail
        {
            Result<ServiceHandleContainer<HandleType>> FindServiceImmediate(
                ServiceIdentifier serviceId, InstanceIdentifier const &instance) noexcept
            {
                if (!Runtime::Get()->IsInitialized())
                {
                    return Result<ServiceHandleContainer<HandleType>>::FromError(
                        ComErrc::kNotInitialized);
                }

                ServiceHandleContainer<std::shared_ptr<BindHandle>> bindHandles;
                for (auto const &name : binding::BindRuntimeNames())
                {
                    auto *bindRuntime = binding::FindBindRuntime(name);
                    if (!bindRuntime)
                    {
                        continue;
                    }
                    ServiceHandleContainer<std::shared_ptr<BindHandle>> snapshot;
                    bindRuntime->GetAvailableServiceHandles(serviceId, instance, snapshot);
                    for (auto &handle : snapshot)
                    {
                        bindHandles.push_back(std::move(handle));
                    }
                }
                return Result<ServiceHandleContainer<HandleType>>::FromValue(Convert(bindHandles));
            }

            Result<ServiceHandleContainer<HandleType>> FindServiceImmediate(
                ServiceIdentifier serviceId) noexcept
            {
                return FindServiceImmediate(serviceId, InstanceIdentifier{0});
            }

            Result<FindServiceHandle> StartFindServiceImpl(
                ServiceIdentifier serviceId,
                FindServiceHandler<HandleType> handler) noexcept
            {
                if (!Runtime::Get()->IsInitialized())
                {
                    return Result<FindServiceHandle>::FromError(ComErrc::kNotInitialized);
                }
                if (!handler)
                {
                    return Result<FindServiceHandle>::FromError(ComErrc::kInvalidArgument);
                }

                const FindServiceHandle handle{serviceId, 0};
                DiscoveryRegistration entry;
                entry.serviceId = serviceId;

                for (auto const &name : binding::BindRuntimeNames())
                {
                    auto *bindRuntime = binding::FindBindRuntime(name);
                    if (!bindRuntime)
                    {
                        continue;
                    }
                    const FindServiceHandle perBinding{serviceId, 0};
                    bindRuntime->RegisterFindServiceHandle(
                        perBinding,
                        [handler, perBinding](
                            ServiceHandleContainer<std::shared_ptr<BindHandle>> const &bindHandles)
                        {
                            handler(Convert(bindHandles), perBinding);
                        });
                    entry.registrations.emplace_back(name, perBinding);
                }

                {
                    auto &registry = DiscoveryRegistry::Instance();
                    std::lock_guard<std::mutex> lock(registry.mutex);
                    registry.entries[handle.uid] = std::move(entry);
                }
                return Result<FindServiceHandle>::FromValue(handle);
            }

            void StopFindServiceImpl(FindServiceHandle const &handle) noexcept
            {
                DiscoveryRegistration entry;
                {
                    auto &registry = DiscoveryRegistry::Instance();
                    std::lock_guard<std::mutex> lock(registry.mutex);
                    auto it = registry.entries.find(handle.uid);
                    if (it == registry.entries.end())
                    {
                        return;
                    }
                    entry = std::move(it->second);
                    registry.entries.erase(it);
                }
                {
                    for (auto const &registration : entry.registrations)
                    {
                        if (auto *bindRuntime = binding::FindBindRuntime(registration.first))
                        {
                            bindRuntime->UnregisterFindServiceHandle(registration.second);
                        }
                    }
                }
            }

            bool IsHandleValid(HandleType const &handle) noexcept
            {
                return handle.IsValid();
            }

            Result<Payload> SendRequestOn(
                HandleType const &handle,
                MethodIdentifier methodId,
                Payload const &payload,
                Duration timeout) noexcept
            {
                auto impl = HandleAccess::Get(handle);
                if (!impl || !impl->handle_)
                {
                    return Result<Payload>::FromError(ComErrc::kServiceNotAvailable);
                }
                return impl->handle_->SendRequest(methodId, payload, timeout);
            }

            Future<Payload> SendRequestAsyncOn(
                HandleType const &handle,
                MethodIdentifier methodId,
                Payload const &payload) noexcept
            {
                auto impl = HandleAccess::Get(handle);
                if (!impl || !impl->handle_)
                {
                    auto promise = std::make_shared<ara::core::Promise<Payload>>();
                    auto future = promise->get_future();
                    promise->SetError(MakeErrorCode(ComErrc::kServiceNotAvailable));
                    return future;
                }
                return impl->handle_->SendRequestAsync(methodId, payload);
            }
        } // namespace detail

    } // namespace proxy
} // namespace com
