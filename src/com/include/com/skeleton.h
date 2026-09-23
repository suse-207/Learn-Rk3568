// Copyright 2024
// Licensed under the Apache License, Version 2.0

/// @file       skeleton.h
/// @brief      Skeleton layer - server side service implementation
/// @details    Contains BindSkeleton and ServiceSkeleton template

#pragma once

#include "com/types.h"
#include "com/runtime.h"
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

            /// @brief Initialize the skeleton
            /// @param[in] instanceIdentifier Instance identifier
            Result<void> Init(InstanceIdentifier const &instanceIdentifier) noexcept
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (initialized_)
                {
                    return Result<void>(true);
                }

                instanceIdentifier_ = instanceIdentifier;

                // Get binding runtimes and create bind skeletons
                auto const &bindRuntimes = Runtime::GetInstance().GetBindRuntimes();
                for (auto const &bindRuntime : bindRuntimes)
                {
                    std::vector<std::unique_ptr<BindSkeleton>> newBindSkeletons;
                    bindRuntime->CreateBindSkeleton(*this, instanceIdentifier_, newBindSkeletons);

                    for (auto &bs : newBindSkeletons)
                    {
                        bindSkeletons_.push_back(std::move(bs));
                    }
                }

                initialized_ = true;
                return Result<void>(true);
            }

            /// @brief Deinitialize the skeleton
            Result<void> Deinit() noexcept
            {
                std::lock_guard<std::mutex> lock(mutex_);
                for (auto &bs : bindSkeletons_)
                {
                    if (bs)
                    {
                        bs->Deinit();
                    }
                }
                bindSkeletons_.clear();
                initialized_ = false;
                return Result<void>(true);
            }

            /// @brief Offer the service on all protocols
            Result<void> Offer() noexcept
            {
                std::lock_guard<std::mutex> lock(mutex_);
                for (auto &bs : bindSkeletons_)
                {
                    if (bs)
                    {
                        auto result = bs->Offer();
                        if (!result)
                        {
                            return result;
                        }
                    }
                }
                return Result<void>(true);
            }

            /// @brief Stop offering the service on all protocols
            Result<void> StopOffer() noexcept
            {
                std::lock_guard<std::mutex> lock(mutex_);
                for (auto &bs : bindSkeletons_)
                {
                    if (bs)
                    {
                        bs->StopOffer();
                    }
                }
                return Result<void>(true);
            }

            /// @brief Get bind skeletons
            std::vector<std::unique_ptr<BindSkeleton>> &GetBindSkeletons() noexcept
            {
                return bindSkeletons_;
            }

        protected:
            InstanceIdentifier instanceIdentifier_;
            std::vector<std::unique_ptr<BindSkeleton>> bindSkeletons_;
            mutable std::mutex mutex_;
            bool initialized_{false};
        };

        /// @brief Service skeleton template
        /// @tparam T Service interface type (user-defined)
        /// @details Application layer inherits from this to implement services.
        ///          This class is protocol-agnostic.
        template <typename T>
        class ServiceSkeleton : public Skeleton, public T
        {
        public:
            /// @brief Constructor
            /// @param[in] serviceIdentifier Service identifier
            ServiceSkeleton(ServiceIdentifier const &serviceIdentifier) noexcept
                : serviceIdentifier_(serviceIdentifier)
            {
            }

            /// @brief Destructor
            ~ServiceSkeleton() override = default;

            /// @brief Get service identifier
            ServiceIdentifier const &GetServiceIdentifier() const noexcept
            {
                return serviceIdentifier_;
            }

        protected:
            ServiceIdentifier serviceIdentifier_;
        };

    } // namespace skeleton
} // namespace com