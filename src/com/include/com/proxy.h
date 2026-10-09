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
#include <thread>
#include <chrono>
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

            /// @brief Send a request and wait for the response
            virtual Result<std::vector<uint8_t>> SendRequest(
                MethodIdentifier methodId,
                std::vector<uint8_t> const &requestData,
                Duration timeout) noexcept = 0;

            /// @brief Send a request asynchronously, returning a Future (P2: ara::com-style)
            virtual Future<std::vector<uint8_t>> SendRequestAsync(
                MethodIdentifier methodId,
                std::vector<uint8_t> const &requestData) noexcept = 0;
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

        /// @brief Service proxy class
        /// @details Application layer uses this to access remote services.
        ///          P3: Optionally accepts an InstanceSpecifier for AUTOSAR-style identification.
        class ServiceProxy
        {
        public:
            /// @brief Constructor with numeric IDs
            ServiceProxy(
                ServiceIdentifier const &serviceIdentifier,
                InstanceIdentifier const &instanceIdentifier) noexcept
                : serviceIdentifier_(serviceIdentifier), instanceIdentifier_(instanceIdentifier), instanceSpecifier_{"unknown"}
            {
            }

            /// @brief Constructor with InstanceSpecifier (P3)
            ServiceProxy(
                InstanceSpecifier const &specifier,
                ServiceIdentifier const &serviceIdentifier,
                InstanceIdentifier const &instanceIdentifier) noexcept
                : serviceIdentifier_(serviceIdentifier),
                  instanceIdentifier_(instanceIdentifier),
                  instanceSpecifier_(specifier)
            {
            }

            ~ServiceProxy() = default;

            Result<void> Init() noexcept
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (initialized_)
                {
                    return Result<void>::FromValue();
                }

                auto *runtime = Runtime::Get();
                if (!runtime)
                {
                    return Result<void>::FromError(ComErrc::kNotInitialized);
                }

                for (auto const &name : runtime->GetBindRuntimeNames())
                {
                    auto *bindRuntime = runtime->GetBindRuntime(name);
                    if (bindRuntime)
                    {
                        ServiceHandleContainer<std::shared_ptr<BindHandle>> bindHandles;
                        bindRuntime->GetAvailableServiceHandles(
                            serviceIdentifier_, instanceIdentifier_, bindHandles);

                        for (auto &handle : bindHandles)
                        {
                            bindHandles_.push_back(std::move(handle));
                        }
                    }
                }

                initialized_ = true;
                return Result<void>::FromValue();
            }

            Result<void> Deinit() noexcept
            {
                std::lock_guard<std::mutex> lock(mutex_);
                bindHandles_.clear();
                initialized_ = false;
                return Result<void>::FromValue();
            }

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

            /// @brief Get instance specifier (P3)
            InstanceSpecifier const &GetInstanceSpecifier() const noexcept
            {
                return instanceSpecifier_;
            }

        protected:
            ServiceIdentifier serviceIdentifier_;
            InstanceIdentifier instanceIdentifier_;
            InstanceSpecifier instanceSpecifier_;
            std::vector<std::shared_ptr<BindHandle>> bindHandles_;
            mutable std::mutex mutex_;
            bool initialized_{false};
        };

    } // namespace proxy
} // namespace com
