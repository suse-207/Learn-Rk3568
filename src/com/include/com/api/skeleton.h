// Copyright 2024
// Licensed under the Apache License, Version 2.0

/// @file       skeleton.h
/// @brief      Skeleton layer - server side service implementation (public API)
/// @details    Contains Skeleton and ServiceSkeleton。绑定类型不属于本头；
///             TODO(P7)：bindSkeletons_ 换成不透明接口后删除 binding/bind_skeleton.h。
///             见 docs/adr/0001-public-api-ownership.md（C2/C13）。

#pragma once

#include "com/api/types.h"
#include "com/api/runtime.h"
#include "com/binding/bind_skeleton.h"  // TODO(P7): 不透明化后移除
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

namespace com
{
    namespace skeleton
    {

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
