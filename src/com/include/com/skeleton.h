// Copyright 2024
// Licensed under the Apache License, Version 2.0

/// @file       skeleton.h
/// @brief      Skeleton layer - server side service implementation
/// @details    Contains BindSkeleton and ServiceSkeleton template

#pragma once

#include "com/types.h"
#include "com/runtime.h"
#include <functional>
#include <memory>
#include <vector>
#include <mutex>

namespace com
{
    namespace skeleton
    {

        /// @brief Skeleton base class (forward declaration)
        class Skeleton;

        /// @brief Binding layer skeleton abstract interface
        /// @details Represents the server-side binding for a service instance.
        ///          Each protocol implementation must provide its own BindSkeleton.
        class BindSkeleton
        {
        public:
            BindSkeleton() noexcept = default;
            virtual ~BindSkeleton() noexcept = default;

            BindSkeleton(BindSkeleton const &) = delete;
            BindSkeleton &operator=(BindSkeleton const &) = delete;

            /// @brief Initialize the bind skeleton
            virtual Result<void> Init() noexcept = 0;

            /// @brief Deinitialize the bind skeleton
            virtual Result<void> Deinit() noexcept = 0;

            /// @brief Offer the service
            virtual Result<void> Offer() noexcept = 0;

            /// @brief Stop offering the service
            virtual Result<void> StopOffer() noexcept = 0;

            /// @brief Register a method handler
            /// @param[in] methodId Method identifier
            /// @param[in] handler Called with (request, response)
            virtual void RegisterMethodHandler(
                MethodIdentifier methodId,
                std::function<void(std::vector<uint8_t> const &, std::vector<uint8_t> &)> handler) noexcept = 0;

            /// @brief Get the binding runtime name
            virtual char const *GetBindRuntimeName() const noexcept = 0;
        };

        /// @brief Skeleton base class
        /// @details Base class for service skeleton implementations.
        ///          Manages multiple BindSkeleton instances for multi-protocol support.
        class Skeleton
        {
        public:
            Skeleton() noexcept = default;
            virtual ~Skeleton() noexcept = default;

            Skeleton(Skeleton const &) = delete;
            Skeleton &operator=(Skeleton const &) = delete;

            ServiceIdentifier const &GetServiceIdentifier() const noexcept
            {
                return serviceIdentifier_;
            }

            void SetServiceIdentifier(ServiceIdentifier serviceIdentifier) noexcept
            {
                serviceIdentifier_ = serviceIdentifier;
            }

            Result<void> Init(InstanceIdentifier const &instanceIdentifier) noexcept
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (initialized_)
                {
                    return Result<void>::FromValue();
                }
                instanceIdentifier_ = instanceIdentifier;
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
                        std::vector<std::unique_ptr<BindSkeleton>> newBindSkeletons;
                        bindRuntime->CreateBindSkeleton(*this, instanceIdentifier_, newBindSkeletons);
                        for (auto &bs : newBindSkeletons)
                        {
                            bindSkeletons_.push_back(std::move(bs));
                        }
                    }
                }
                initialized_ = true;
                return Result<void>::FromValue();
            }

            Result<void> Deinit() noexcept
            {
                std::lock_guard<std::mutex> lock(mutex_);
                for (auto &bs : bindSkeletons_)
                {
                    if (bs) { bs->Deinit(); }
                }
                bindSkeletons_.clear();
                initialized_ = false;
                return Result<void>::FromValue();
            }

            Result<void> Offer() noexcept
            {
                std::lock_guard<std::mutex> lock(mutex_);
                for (auto &bs : bindSkeletons_)
                {
                    if (bs)
                    {
                        auto result = bs->Offer();
                        if (!result) { return result; }
                    }
                }
                return Result<void>::FromValue();
            }

            Result<void> StopOffer() noexcept
            {
                std::lock_guard<std::mutex> lock(mutex_);
                for (auto &bs : bindSkeletons_)
                {
                    if (bs) { bs->StopOffer(); }
                }
                return Result<void>::FromValue();
            }

            std::vector<std::unique_ptr<BindSkeleton>> &GetBindSkeletons() noexcept
            {
                return bindSkeletons_;
            }

        protected:
            ServiceIdentifier serviceIdentifier_{0};
            InstanceIdentifier instanceIdentifier_;
            std::vector<std::unique_ptr<BindSkeleton>> bindSkeletons_;
            mutable std::mutex mutex_;
            bool initialized_{false};
        };

        /// @brief Service skeleton base class
        /// @details Application layer inherits from this to implement services.
        ///          P3: Optionally accepts an InstanceSpecifier for AUTOSAR-style identification.
        class ServiceSkeleton : public Skeleton
        {
        public:
            /// @brief Constructor with numeric IDs
            ServiceSkeleton(ServiceIdentifier const &serviceIdentifier,
                            InstanceIdentifier const &instanceIdentifier) noexcept
                : instanceSpecifier_{"unknown"}
            {
                SetServiceIdentifier(serviceIdentifier);
                instanceIdentifier_ = instanceIdentifier;
            }

            /// @brief Constructor with InstanceSpecifier (P3)
            ServiceSkeleton(InstanceSpecifier const &specifier,
                            ServiceIdentifier const &serviceIdentifier,
                            InstanceIdentifier const &instanceIdentifier) noexcept
                : instanceSpecifier_(specifier)
            {
                SetServiceIdentifier(serviceIdentifier);
                instanceIdentifier_ = instanceIdentifier;
            }

            ~ServiceSkeleton() override = default;

            /// @brief Get instance specifier (P3)
            InstanceSpecifier const &GetInstanceSpecifier() const noexcept
            {
                return instanceSpecifier_;
            }

        protected:
            InstanceSpecifier instanceSpecifier_;
        };

    } // namespace skeleton
} // namespace com
