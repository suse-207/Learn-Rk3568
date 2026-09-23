// Copyright 2024
// Licensed under the Apache License, Version 2.0

/// @file       vsomeip_bind_handle.h
/// @brief      vsomeip binding handle implementation
/// @details    Implements BindHandle interface for vsomeip

#pragma once

#include "com/proxy.h"
#include <vsomeip/application.hpp>
#include <vsomeip/runtime.hpp>
#include <memory>

namespace com
{
    namespace vsomeip_binding
    {

        /// @brief vsomeip binding handle implementation
        /// @details Represents a connection to a SOME/IP service instance via vsomeip
        class VsomeipBindHandle : public proxy::BindHandle
        {
        public:
            /// @brief Constructor
            /// @param[in] vsomeipApp vsomeip application
            /// @param[in] serviceId Service identifier
            /// @param[in] instanceId Instance identifier
            VsomeipBindHandle(
                std::shared_ptr<vsomeip_v3::application> vsomeipApp,
                ServiceIdentifier serviceId,
                InstanceIdentifier instanceId) noexcept;

            /// @brief Destructor
            ~VsomeipBindHandle() noexcept override;

            // BindHandle interface
            bool IsValid() const noexcept override;
            char const *GetBindRuntimeName() const noexcept override;

            /// @brief Send a request and wait for response
            /// @param[in] methodId Method identifier
            /// @param[in] requestData Request data
            /// @param[in] timeout Timeout duration
            /// @return Response data
            Result<std::vector<uint8_t>> SendRequest(
                MethodIdentifier methodId,
                std::vector<uint8_t> const &requestData,
                Duration timeout) noexcept;

            /// @brief Send a request asynchronously
            /// @param[in] methodId Method identifier
            /// @param[in] requestData Request data
            /// @param[in] handler Response handler callback
            void SendRequestAsync(
                MethodIdentifier methodId,
                std::vector<uint8_t> const &requestData,
                std::function<void(Result<std::vector<uint8_t>>)> handler) noexcept;

            /// @brief Subscribe to an event group
            /// @param[in] eventGroupId Event group identifier
            /// @param[in] handler Event notification handler
            void SubscribeEvent(
                EventIdentifier eventGroupId,
                std::function<void(std::vector<uint8_t> const &)> handler) noexcept;

            /// @brief Unsubscribe from an event group
            /// @param[in] eventGroupId Event group identifier
            void UnsubscribeEvent(EventIdentifier eventGroupId) noexcept;

            /// @brief Get service identifier
            ServiceIdentifier GetServiceId() const noexcept { return serviceId_; }

            /// @brief Get instance identifier
            InstanceIdentifier GetInstanceId() const noexcept { return instanceId_; }

        private:
            std::shared_ptr<vsomeip_v3::application> vsomeipApp_;
            ServiceIdentifier serviceId_;
            InstanceIdentifier instanceId_;
            bool valid_{false};
        };

    } // namespace vsomeip_binding
} // namespace com