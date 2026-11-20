// Copyright 2024
// Licensed under the Apache License, Version 2.0

/// @file       vsomeip_request_dispatcher.h
/// @brief      vsomeip 请求分发器
/// @details    在 runtime 级持有在途请求表，按 SOME/IP session 精确匹配响应。
///
///             vsomeip 对 (service, instance, method) 只保留一个 message handler，
///             作用域是 application。因此每个组合只注册一次稳定 handler，请求状态
///             也放在这一层，而不是放在每请求新建的句柄里。
///
///             发送采用两阶段登记：先在锁内登记占位 token，释放锁后 send()，
///             拿到 vsomeip 分配的 session 后再在锁内把 token 与 session 关联。
///             send 与迁移之间到达的响应暂存于 earlyResponses，绑定后立即交付。
///             所有 Promise 的交付都在锁外完成（Promise::SetResult 会同步执行
///             用户续体，持锁交付会自死锁）。

#pragma once

#include "com/types.h"
#include <ara/core/future.h>
#include <ara/core/promise.h>
#include <vsomeip/application.hpp>
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
            explicit VsomeipRequestDispatcher(
                std::shared_ptr<vsomeip_v3::application> vsomeipApp) noexcept;

            ~VsomeipRequestDispatcher() noexcept;

            VsomeipRequestDispatcher(VsomeipRequestDispatcher const &) = delete;
            VsomeipRequestDispatcher &operator=(VsomeipRequestDispatcher const &) = delete;

            /// @brief 同步发送并等待响应
            /// @return 响应载荷；超时返回 kTimeout，传输/响应异常返回 kCommunicationError
            Result<std::vector<uint8_t>> Send(
                ServiceIdentifier serviceId,
                InstanceIdentifier instanceId,
                MethodIdentifier methodId,
                std::vector<uint8_t> const &requestData,
                Duration timeout) noexcept;

            /// @brief 异步发送：响应到达、超时或运行时停止时完成
            Future<std::vector<uint8_t>> SendAsync(
                ServiceIdentifier serviceId,
                InstanceIdentifier instanceId,
                MethodIdentifier methodId,
                std::vector<uint8_t> const &requestData) noexcept;

            /// @brief 停止：拒绝新请求、以错误结束全部在途请求、注销全部 method handler
            /// @details 幂等，可重复调用
            void Stop() noexcept;

        private:
            class Impl;
            std::shared_ptr<Impl> impl_;
        };

    } // namespace vsomeip_binding
} // namespace com
