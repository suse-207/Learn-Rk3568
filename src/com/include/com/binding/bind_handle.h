// Copyright 2024
// Licensed under the Apache License, Version 2.0

/// @file       bind_handle.h
/// @brief      Binding layer client-side handles (protocol SPI)
/// @details    BindHandle / BindProxy 属绑定 SPI，业务层不得直接包含；
///             公共 API 只经 com/api/proxy.h 暴露 ServiceProxy。
///
///             超时归属（ADR 0001 C7）：绑定层**不认识 Duration**。
///             绑定层只提供 Submit()（登记 + 发送，返回 future 与 token）和
///             Drop(token)（调用方放弃某条在途请求）；同步等待与到期判定
///             全部在下面的非虚便利函数里由调用线程完成，绑定实现无从覆写。

#pragma once

#include "com/api/types.h"
#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace com
{
    namespace proxy
    {

        /// @brief 在途请求标识（绑定层分配，仅用于 Drop）
        using SubmissionToken = std::uint64_t;

        /// @brief 提交结果
        /// @details token 为空表示提交阶段即失败，错误已经写进 future。
        struct Submission
        {
            Future<Payload> future;
            std::optional<SubmissionToken> token;
        };

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

            /// @brief Instance identifier of this handle (SWS_CM_00312 HandleType)
            virtual InstanceIdentifier GetInstanceId() const noexcept = 0;

            /// @brief 提交一个请求：绑定层只负责登记与发送，不关心超时
            virtual Submission Submit(
                MethodIdentifier methodId,
                Payload const &requestData) noexcept = 0;

            /// @brief 调用方放弃某条在途请求（到期时使用）
            /// @return 是否真的摘下（false 表示已被响应或 Stop() 结算）
            virtual bool Drop(SubmissionToken token) noexcept = 0;

            /// @brief 同步请求：超时归调用线程（非虚，绑定实现无权覆写超时语义）
            /// @details 等价于 Submit + 在调用线程上等 + 到期 Drop 自己那条。
            Result<Payload> SendRequest(
                MethodIdentifier methodId,
                Payload const &requestData,
                Duration timeout) noexcept
            {
                auto submission = Submit(methodId, requestData);
                if (!submission.token.has_value())
                {
                    // 提交阶段失败，错误已经写进 future
                    return submission.future.GetResult();
                }
                if (submission.future.wait_for(timeout) == ara::core::future_status::ready)
                {
                    return submission.future.GetResult();
                }
                // 到期：自己摘下自己那条；若已被响应或 Stop() 结算，以其结果为准
                if (Drop(*submission.token))
                {
                    return Result<Payload>::FromError(ComErrc::kTimeout);
                }
                return submission.future.GetResult();
            }

            /// @brief 异步请求：不承诺超时（响应到达或 Stop() 时结算）
            Future<Payload> SendRequestAsync(
                MethodIdentifier methodId,
                Payload const &requestData) noexcept
            {
                return Submit(methodId, requestData).future;
            }

            /// @brief Subscribe to an event group and register a notification handler
            /// @param[in] eventId Event identifier
            /// @param[in] eventGroupId Event group the event belongs to
            /// @param[in] handler Called with the serialized event payload
            virtual void SubscribeEvent(
                EventIdentifier eventId,
                EventGroupIdentifier eventGroupId,
                std::function<void(Payload const &)> handler) noexcept = 0;

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

    } // namespace proxy
} // namespace com
