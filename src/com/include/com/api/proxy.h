// Copyright 2024
// Licensed under the Apache License, Version 2.0

/// @file       proxy.h
/// @brief      Proxy layer - client side service access (public API)
/// @details    只有声明；实现与发现状态在 src/com/src/api/proxy.cpp（pimpl）。
///             本头不包含、也不前置声明任何绑定类型。
///             见 docs/adr/0001-public-api-ownership.md（C2/C3）。

#pragma once

#include "com/api/types.h"
#include <memory>

namespace com
{
    namespace proxy
    {

        /// @brief Service proxy class
        /// @details Application layer uses this to access remote services.
        ///          P3: Optionally accepts an InstanceSpecifier for AUTOSAR-style identification.
        /// @note 不可拷贝、不可移动：内部持有发现回调注册，与对象地址绑定。
        class ServiceProxy
        {
        public:
            /// @brief Constructor with numeric IDs
            ServiceProxy(
                ServiceIdentifier const &serviceIdentifier,
                InstanceIdentifier const &instanceIdentifier) noexcept;

            /// @brief Constructor with InstanceSpecifier (P3)
            ServiceProxy(
                InstanceSpecifier const &specifier,
                ServiceIdentifier const &serviceIdentifier,
                InstanceIdentifier const &instanceIdentifier) noexcept;

            /// @brief 析构时自动 Deinit（注销发现回调，避免留下悬空回调）
            ~ServiceProxy();

            ServiceProxy(ServiceProxy const &) = delete;
            ServiceProxy &operator=(ServiceProxy const &) = delete;
            ServiceProxy(ServiceProxy &&) = delete;
            ServiceProxy &operator=(ServiceProxy &&) = delete;

            /// @brief 注册发现回调并取首次快照；幂等
            Result<void> Init() noexcept;

            /// @brief 注销发现回调并清空发现状态；幂等
            Result<void> Deinit() noexcept;

            /// @brief 当前是否有可用绑定
            bool IsAvailable() const noexcept;

            /// @brief 等待服务可用（事件驱动，无轮询）
            bool WaitForAvailable(Duration timeout) noexcept;

            /// @brief Get instance specifier (P3)
            InstanceSpecifier const &GetInstanceSpecifier() const noexcept;

        private:
            class Impl;
            std::unique_ptr<Impl> impl_;
        };

    } // namespace proxy
} // namespace com
