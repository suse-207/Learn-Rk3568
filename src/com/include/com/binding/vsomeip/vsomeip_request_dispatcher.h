// Copyright 2024
// Licensed under the Apache License, Version 2.0

/// @file       vsomeip_request_dispatcher.h
/// @brief      vsomeip 请求分发器
/// @details    在 binding 层的 runtime 级持有在途请求表，按 SOME/IP session 精确匹配响应。
///
///             vsomeip 对 (service, instance, method) 只保留一个 message handler，
///             作用域是 application。因此每个组合只注册一次稳定 handler，请求状态
///             也放在这一层，而不是放在每请求新建的句柄里。
///
///             线程模型（重要）：本类**不拥有任何线程或 loop**。
///               - 调用线程：提交与同步等待；
///               - vsomeip io 线程：消息回调；
///               - 超时：同步请求由调用线程自己计时并摘下自己那条；
///                 异步请求不承诺超时（没有 loop 就没有人能推进 deadline）。
///             将来的异步超时只能由“持有 loop 的一方”驱动，本类不内建定时器。

#pragma once

#include "com/api/types.h"
#include <ara/core/future.h>
#include <ara/core/promise.h>
#include <vsomeip/application.hpp>
#include <chrono>
#include <cstdint>
#include <memory>
#include <vector>

namespace com
{
    namespace vsomeip_binding
    {

        /// @brief 请求分发器：runtime 级共享，所有 BindHandle 共用
        class VsomeipRequestDispatcher
        {
        public:
            /// @brief 运行统计（只读快照）
            struct Stats
            {
                std::uint64_t submitted{0};     ///< 累计提交的请求数
                std::uint64_t inFlight{0};      ///< 当前在途请求数
                std::uint64_t timedOut{0};      ///< 同步调用方到期摘下自己那条的次数
                std::uint64_t droppedLate{0};   ///< 迟到响应被丢弃的数量
                std::uint64_t droppedEarly{0};  ///< 无法归类的响应被丢弃的数量
            };

            explicit VsomeipRequestDispatcher(
                std::shared_ptr<vsomeip_v3::application> vsomeipApp) noexcept;

            ~VsomeipRequestDispatcher() noexcept;

            VsomeipRequestDispatcher(VsomeipRequestDispatcher const &) = delete;
            VsomeipRequestDispatcher &operator=(VsomeipRequestDispatcher const &) = delete;

            /// @brief 同步发送并等待响应
            /// @details 超时由调用线程拥有：到期时摘下自己那条、记墓碑、返回 kTimeout
            Result<std::vector<uint8_t>> Send(
                ServiceIdentifier serviceId,
                InstanceIdentifier instanceId,
                MethodIdentifier methodId,
                std::vector<uint8_t> const &requestData,
                Duration timeout) noexcept;

            /// @brief 异步发送
            /// @details 不承诺超时：只在响应到达或 Stop() 时结算
            Future<std::vector<uint8_t>> SendAsync(
                ServiceIdentifier serviceId,
                InstanceIdentifier instanceId,
                MethodIdentifier methodId,
                std::vector<uint8_t> const &requestData) noexcept;

            /// @brief 停止：拒绝新请求、以错误结束全部在途请求、注销全部 method handler
            /// @details 幂等，可重复调用
            void Stop() noexcept;

            /// @brief 读取运行统计快照
            Stats GetStats() const noexcept;

        private:
            class Impl;
            std::shared_ptr<Impl> impl_;
        };

    } // namespace vsomeip_binding
} // namespace com
