// Copyright 2024
// Licensed under the Apache License, Version 2.0

/// @file       vsomeip_bind_handle.h
/// @brief      vsomeip binding handle implementation
/// @details    Implements BindHandle interface for vsomeip

#pragma once

#include "com/binding/bind_handle.h"
#include <vsomeip/application.hpp>
#include <vsomeip/runtime.hpp>
#include <memory>

namespace com
{
    namespace vsomeip_binding
    {

        // 绑定 SPI 的提交结果与在途 token
        using proxy::Submission;
        using proxy::SubmissionToken;

        class VsomeipRequestDispatcher;

        /// @brief vsomeip binding handle implementation
        /// @details Represents a connection to a SOME/IP service instance via vsomeip.
        ///          在途请求状态由 runtime 级的 VsomeipRequestDispatcher 持有，
        ///          本类只做参数校验与转发。
        class VsomeipBindHandle : public proxy::BindHandle
        {
        public:
            /// @brief Constructor
            /// @param[in] vsomeipApp vsomeip application
            /// @param[in] dispatcher runtime 级请求分发器，所有 handle 共用
            /// @param[in] serviceId Service identifier
            /// @param[in] instanceId Instance identifier
            VsomeipBindHandle(
                std::shared_ptr<vsomeip_v3::application> vsomeipApp,
                std::shared_ptr<VsomeipRequestDispatcher> dispatcher,
                ServiceIdentifier serviceId,
                InstanceIdentifier instanceId) noexcept;

            /// @brief Destructor
            ~VsomeipBindHandle() noexcept override;

            // BindHandle interface
            bool IsValid() const noexcept override;
            char const *GetBindRuntimeName() const noexcept override;

            /// @brief 提交请求（绑定层不认识超时，ADR 0001 C7）
            Submission Submit(
                MethodIdentifier methodId,
                std::vector<uint8_t> const &requestData) noexcept override;

            /// @brief 调用方放弃在途请求
            bool Drop(SubmissionToken token) noexcept override;

            /// @brief Subscribe to an event
            /// @param[in] eventId Event identifier
            /// @param[in] eventGroupId Event group identifier the event belongs to
            /// @param[in] handler Event notification handler
            void SubscribeEvent(
                EventIdentifier eventId,
                EventGroupIdentifier eventGroupId,
                std::function<void(std::vector<uint8_t> const &)> handler) noexcept override;

            /// @brief Unsubscribe from an event
            /// @param[in] eventId Event identifier
            /// @param[in] eventGroupId Event group identifier the event belongs to
            void UnsubscribeEvent(
                EventIdentifier eventId,
                EventGroupIdentifier eventGroupId) noexcept override;

            /// @brief Get service identifier
            ServiceIdentifier GetServiceId() const noexcept { return serviceId_; }

            /// @brief Get instance identifier
            InstanceIdentifier GetInstanceId() const noexcept override { return instanceId_; }

        private:
            std::shared_ptr<vsomeip_v3::application> vsomeipApp_;
            std::shared_ptr<VsomeipRequestDispatcher> dispatcher_;
            ServiceIdentifier serviceId_;
            InstanceIdentifier instanceId_;
            bool valid_{false};
        };

    } // namespace vsomeip_binding
} // namespace com
