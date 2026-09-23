// Copyright 2024
// Licensed under the Apache License, Version 2.0

/// @file       vsomeip_bind_runtime.h
/// @brief      vsomeip binding runtime implementation
/// @details    Implements BindRuntime interface using vsomeip library

#pragma once

#include "com/bind_runtime.h"
#include <vsomeip/runtime.hpp>
#include <vsomeip/application.hpp>
#include <memory>
#include <string>

namespace com
{
    namespace vsomeip_binding
    {

        /// @brief vsomeip binding runtime implementation
        /// @details Implements the BindRuntime interface using the vsomeip library.
        ///          This allows CAPI applications to communicate via SOME/IP protocol
        ///          using the open-source vsomeip stack.
        class VsomeipBindRuntime : public BindRuntime
        {
        public:
            /// @brief Constructor
            /// @param[in] applicationName vsomeip application name
            explicit VsomeipBindRuntime(std::string const &applicationName) noexcept;

            /// @brief Destructor
            ~VsomeipBindRuntime() noexcept override;

            // Delete copy/move
            VsomeipBindRuntime(VsomeipBindRuntime const &) = delete;
            VsomeipBindRuntime &operator=(VsomeipBindRuntime const &) = delete;

            /// @brief Initialize the vsomeip runtime
            Result<void> Init() noexcept;

            /// @brief Deinitialize the vsomeip runtime
            Result<void> Deinit() noexcept;

            /// @brief Start the vsomeip application (blocking)
            void Start() noexcept;

            /// @brief Stop the vsomeip application
            void Stop() noexcept;

            // BindRuntime interface implementation
            void CreateBindSkeleton(
                skeleton::Skeleton &skeleton,
                InstanceIdentifier const &instanceIdentifier,
                std::vector<std::unique_ptr<skeleton::BindSkeleton>> &bindSkeletons) noexcept override;

            void GetAvailableServiceHandles(
                ServiceIdentifier const &serviceIdentifier,
                InstanceIdentifier const &instanceIdentifier,
                ServiceHandleContainer<std::shared_ptr<proxy::BindHandle>> &bindHandles) noexcept override;

            void RegisterFindServiceHandle(
                FindServiceHandle const &findHandle,
                FindServiceHandler<std::shared_ptr<proxy::BindHandle>> const &findHandler) noexcept override;

            void UnregisterFindServiceHandle(FindServiceHandle const &findHandle) noexcept override;

            char const *GetName() const noexcept override;

            /// @brief Get the underlying vsomeip runtime
            std::shared_ptr<vsomeip_v3::runtime> GetVsomeipRuntime() const noexcept;

            /// @brief Get the vsomeip application
            std::shared_ptr<vsomeip_v3::application> GetVsomeipApplication() const noexcept;

        private:
            class Impl;
            std::unique_ptr<Impl> impl_;
        };

    } // namespace vsomeip_binding
} // namespace com