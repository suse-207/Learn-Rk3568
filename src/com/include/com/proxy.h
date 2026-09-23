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
#include <mutex>
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

        /// @brief Service proxy base class
        /// @tparam T Service interface type (user-defined)
        /// @details Application layer uses this to access remote services.
        ///          This class is protocol-agnostic.
        template <typename T>
        class ServiceProxy : public T
        {
        public:
            /// @brief Constructor
            /// @param[in] serviceIdentifier Service identifier
            /// @param[in] instanceIdentifier Instance identifier
            ServiceProxy(
                ServiceIdentifier const &serviceIdentifier,
                InstanceIdentifier const &instanceIdentifier) noexcept
                : serviceIdentifier_(serviceIdentifier), instanceIdentifier_(instanceIdentifier)
            {
            }

            /// @brief Destructor
            ~ServiceProxy() override = default;

            /// @brief Initialize the service proxy
            Result<void> Init() noexcept
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (initialized_)
                {
                    return Result<void>(true);
                }

                // Get binding runtimes and create bind proxies
                auto const &bindRuntimes = Runtime::GetInstance().GetBindRuntimes();
                for (auto const &bindRuntime : bindRuntimes)
                {
                    // Service discovery - find available service handles
                    ServiceHandleContainer<std::shared_ptr<BindHandle>> bindHandles;
                    bindRuntime->GetAvailableServiceHandles(
                        serviceIdentifier_, instanceIdentifier_, bindHandles);

                    // Store handles for later use
                    for (auto &handle : bindHandles)
                    {
                        bindHandles_.push_back(std::move(handle));
                    }
                }

                initialized_ = true;
                return Result<void>(true);
            }

            /// @brief Deinitialize the service proxy
            Result<void> Deinit() noexcept
            {
                std::lock_guard<std::mutex> lock(mutex_);
                bindHandles_.clear();
                initialized_ = false;
                return Result<void>(true);
            }

            /// @brief Check if service is available
            bool IsAvailable() const noexcept
            {
                std::lock_guard<std::mutex> lock(mutex_);
                for (auto const &handle : bindHandles_)
                {
                    if (handle && handle->IsValid())
                    {
                        return true;
                    }
                }
                return false;
            }

            /// @brief Wait for service availability
            bool WaitForAvailable(Duration timeout) noexcept
            {
                auto start = std::chrono::steady_clock::now();
                while (!IsAvailable())
                {
                    auto elapsed = std::chrono::steady_clock::now() - start;
                    if (elapsed >= timeout)
                    {
                        return false;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
                return true;
            }

        protected:
            ServiceIdentifier serviceIdentifier_;
            InstanceIdentifier instanceIdentifier_;
            std::vector<std::shared_ptr<BindHandle>> bindHandles_;
            mutable std::mutex mutex_;
            bool initialized_{false};
        };

    } // namespace proxy
} // namespace com