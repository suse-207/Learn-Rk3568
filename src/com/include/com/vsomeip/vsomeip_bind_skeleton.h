// Copyright 2024
// Licensed under the Apache License, Version 2.0

/// @file       vsomeip_bind_skeleton.h
/// @brief      vsomeip binding skeleton implementation
/// @details    Implements BindSkeleton interface for vsomeip

#pragma once

#include "com/skeleton.h"
#include <vsomeip/application.hpp>
#include <vsomeip/runtime.hpp>
#include <memory>
#include <functional>

namespace com
{
    namespace vsomeip_binding
    {

        /// @brief vsomeip binding skeleton implementation
        /// @details Represents the server-side binding for a SOME/IP service via vsomeip
        class VsomeipBindSkeleton : public skeleton::BindSkeleton
        {
        public:
            /// @brief Constructor
            /// @param[in] vsomeipApp vsomeip application
            /// @param[in] skeleton Parent skeleton
            /// @param[in] serviceId Service identifier
            /// @param[in] instanceId Instance identifier
            VsomeipBindSkeleton(
                std::shared_ptr<vsomeip_v3::application> vsomeipApp,
                skeleton::Skeleton &skeleton,
                ServiceIdentifier serviceId,
                InstanceIdentifier instanceId) noexcept;

            /// @brief Destructor
            ~VsomeipBindSkeleton() noexcept override;

            // BindSkeleton interface
            Result<void> Init() noexcept override;
            Result<void> Deinit() noexcept override;
            Result<void> Offer() noexcept override;
            Result<void> StopOffer() noexcept override;
            char const *GetBindRuntimeName() const noexcept override;

            /// @brief Register method handler
            /// @param[in] methodId Method identifier
            /// @param[in] handler Method handler callback
            void RegisterMethodHandler(
                MethodIdentifier methodId,
                std::function<void(std::vector<uint8_t> const &, std::vector<uint8_t> &)> handler) noexcept;

            /// @brief Send event notification
            /// @param[in] eventId Event identifier
            /// @param[in] eventData Event data
            void SendEvent(EventIdentifier eventId, std::vector<uint8_t> const &eventData) noexcept;

            /// @brief Get service identifier
            ServiceIdentifier GetServiceId() const noexcept { return serviceId_; }

            /// @brief Get instance identifier
            InstanceIdentifier GetInstanceId() const noexcept { return instanceId_; }

        private:
            std::shared_ptr<vsomeip_v3::application> vsomeipApp_;
            skeleton::Skeleton &skeleton_;
            ServiceIdentifier serviceId_;
            InstanceIdentifier instanceId_;
            bool offered_{false};
        };

    } // namespace vsomeip_binding
} // namespace com