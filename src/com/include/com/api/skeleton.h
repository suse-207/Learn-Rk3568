// Copyright 2024
// Licensed under the Apache License, Version 2.0

/// @file       skeleton.h
/// @brief      Skeleton layer - server side service implementation (public API)
/// @details    只有声明；绑定骨架集合与生命周期在 src/com/src/api/skeleton.cpp（pimpl）。
///             本头不包含、也不前置声明任何绑定类型；原 GetBindSkeletons() 已删除
///             （零调用，且它把 BindSkeleton 暴露到公共 API）。
///             见 docs/adr/0001-public-api-ownership.md（C2/C13/C14）。

#pragma once

#include "com/api/types.h"
#include <memory>

namespace com
{
    namespace skeleton
    {

        /// @brief Skeleton base class
        /// @details Base class for service skeleton implementations.
        ///          Manages the per-binding skeletons internally; they are not exposed.
        class Skeleton
        {
        public:
            Skeleton() noexcept;

            /// @brief 析构时自动 Deinit（释放各绑定的骨架）
            virtual ~Skeleton();

            Skeleton(Skeleton const &) = delete;
            Skeleton &operator=(Skeleton const &) = delete;
            Skeleton(Skeleton &&) = delete;
            Skeleton &operator=(Skeleton &&) = delete;

            ServiceIdentifier const &GetServiceIdentifier() const noexcept;

            /// @brief 为所有已注册绑定创建骨架；幂等
            Result<void> Init(InstanceIdentifier const &instanceIdentifier) noexcept;

            /// @brief 销毁各绑定的骨架；幂等
            Result<void> Deinit() noexcept;

            /// @brief 向所有绑定 Offer 服务
            Result<void> Offer() noexcept;

            /// @brief 向所有绑定 StopOffer 服务
            Result<void> StopOffer() noexcept;

        protected:
            void SetServiceIdentifier(ServiceIdentifier serviceIdentifier) noexcept;
            void SetInstanceIdentifier(InstanceIdentifier instanceIdentifier) noexcept;

        private:
            class Impl;
            std::unique_ptr<Impl> impl_;
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
                SetInstanceIdentifier(instanceIdentifier);
            }

            /// @brief Constructor with InstanceSpecifier (P3)
            ServiceSkeleton(InstanceSpecifier const &specifier,
                            ServiceIdentifier const &serviceIdentifier,
                            InstanceIdentifier const &instanceIdentifier) noexcept
                : instanceSpecifier_(specifier)
            {
                SetServiceIdentifier(serviceIdentifier);
                SetInstanceIdentifier(instanceIdentifier);
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
