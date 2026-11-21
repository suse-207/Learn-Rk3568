// Copyright 2024
// Licensed under the Apache License, Version 2.0

/// @file       proxy.h
/// @brief      Proxy layer - client side service access (public API)
/// @details    本头只出现公共类型：HandleType、ServiceProxy<TProxy> 与若干 detail 声明。
///             绑定向导完全藏在 src/com/src/api/proxy.cpp。
///             见 docs/adr/0001-public-api-ownership.md（C2/C3/C9）。

#pragma once

#include "com/api/types.h"
#include <functional>
#include <memory>

namespace com
{
    namespace proxy
    {

        class HandleType;

        namespace detail
        {
            /// @brief 句柄实现（只在 proxy.cpp 定义，公共头看不到其内容）
            class HandleImpl;

            /// @brief 唯一能构造/取出 HandleImpl 的通道（定义在 proxy.cpp）
            struct HandleAccess
            {
                static HandleType Make(std::shared_ptr<HandleImpl> impl) noexcept;
                static std::shared_ptr<HandleImpl> Get(HandleType const &handle) noexcept;
            };

            Result<ServiceHandleContainer<HandleType>> FindServiceImmediate(
                ServiceIdentifier serviceId) noexcept;
            Result<ServiceHandleContainer<HandleType>> FindServiceImmediate(
                ServiceIdentifier serviceId, InstanceIdentifier const &instance) noexcept;
            Result<FindServiceHandle> StartFindServiceImpl(
                ServiceIdentifier serviceId,
                FindServiceHandler<HandleType> handler) noexcept;
            void StopFindServiceImpl(FindServiceHandle const &handle) noexcept;

            bool IsHandleValid(HandleType const &handle) noexcept;
            Result<Payload> SendRequestOn(
                HandleType const &handle,
                MethodIdentifier methodId,
                Payload const &payload,
                Duration timeout) noexcept;
            Future<Payload> SendRequestAsyncOn(
                HandleType const &handle,
                MethodIdentifier methodId,
                Payload const &payload) noexcept;
        } // namespace detail

        /// @brief 服务代理句柄（SWS_CM_00312）
        /// @details 包装绑定向导，仅暴露实例标识；业务层只需要这一个类型。
        class HandleType
        {
        public:
            HandleType() noexcept;
            ~HandleType() noexcept;
            HandleType(HandleType const &) noexcept;
            HandleType &operator=(HandleType const &) noexcept;
            HandleType(HandleType &&) noexcept;
            HandleType &operator=(HandleType &&) noexcept;

            /// @brief 是否持有有效句柄
            explicit operator bool() const noexcept;

            /// @brief 句柄当前是否可用（服务在线）
            bool IsValid() const noexcept;

            /// @brief 实例标识
            InstanceIdentifier const &GetInstanceId() const noexcept;

            bool operator==(HandleType const &other) const noexcept;
            bool operator<(HandleType const &other) const noexcept;

        private:
            friend struct detail::HandleAccess;
            explicit HandleType(std::shared_ptr<detail::HandleImpl> impl) noexcept;
            std::shared_ptr<detail::HandleImpl> impl_;
        };

        /// @brief 服务代理模板（SWS_CM_00122 / 00622 / 11326 / 10383）
        /// @details 生成的代理形如：
        ///              class VehicleStatusProxy : public com::proxy::ServiceProxy<VehicleStatusProxy>
        ///          并在类内声明 `static constexpr ServiceIdentifier serviceIdentifier`。
        /// @tparam TProxy 生成的代理类型
        template <typename TProxy>
        class ServiceProxy
        {
        public:
            using Handle = HandleType;

            /// @brief 命名构造（SWS_CM_11326 / 10438）
            static Result<TProxy> Create(Handle const &handle) noexcept
            {
                if (!detail::IsHandleValid(handle))
                {
                    return Result<TProxy>::FromError(ComErrc::kServiceNotAvailable);
                }
                return Result<TProxy>::FromValue(TProxy{handle});
            }

            /// @brief 立即发现：任意实例（SWS_CM_00122）
            static Result<ServiceHandleContainer<Handle>> FindService() noexcept
            {
                return detail::FindServiceImmediate(TProxy::serviceIdentifier);
            }

            /// @brief 立即发现：指定实例（SWS_CM_00122）
            static Result<ServiceHandleContainer<Handle>> FindService(
                InstanceIdentifier const &instance) noexcept
            {
                return detail::FindServiceImmediate(TProxy::serviceIdentifier, instance);
            }

            /// @brief 异步发现：上线/下线都通过 handler 通知（SWS_CM_00303）
            static Result<FindServiceHandle> StartFindService(
                FindServiceHandler<Handle> handler) noexcept
            {
                return detail::StartFindServiceImpl(TProxy::serviceIdentifier, std::move(handler));
            }

            /// @brief 停止某一路异步发现
            static void StopFindService(FindServiceHandle const &handle) noexcept
            {
                detail::StopFindServiceImpl(handle);
            }

            /// @brief 取本代理的句柄（SWS_CM_10383）
            Handle GetHandle() const noexcept { return handle_; }

            /// @brief 句柄当前是否可用
            bool IsAvailable() const noexcept { return detail::IsHandleValid(handle_); }

        protected:
            explicit ServiceProxy(Handle const &handle) noexcept : handle_(handle) {}
            ~ServiceProxy() noexcept = default;

            ServiceProxy(ServiceProxy const &) = delete;
            ServiceProxy &operator=(ServiceProxy const &) = delete;
            ServiceProxy(ServiceProxy &&) = delete;
            ServiceProxy &operator=(ServiceProxy &&) = delete;

            /// @brief 同步请求：超时归调用线程（ADR C7）
            Result<Payload> SendRequest(
                MethodIdentifier methodId,
                Payload const &payload,
                Duration timeout) noexcept
            {
                return detail::SendRequestOn(handle_, methodId, payload, timeout);
            }

            /// @brief 异步请求：不承诺超时
            Future<Payload> SendRequestAsync(
                MethodIdentifier methodId,
                Payload const &payload) noexcept
            {
                return detail::SendRequestAsyncOn(handle_, methodId, payload);
            }

        private:
            Handle handle_;
        };

    } // namespace proxy
} // namespace com
