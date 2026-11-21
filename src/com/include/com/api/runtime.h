// Copyright 2024
// Licensed under the Apache License, Version 2.0

/// @file       runtime.h
/// @brief      Communication runtime (public API)
/// @details    应用只调用 Initialize(applicationName)：绑定由 com 自己按编译期选择装配，
///             应用代码里不再出现任何协议名。绑定层的枚举/查找属于绑定内部，
///             见 com/binding/bind_runtime_registry.h。
///             见 docs/adr/0001-public-api-ownership.md（C19）。

#pragma once

#include "com/api/types.h"
#include <string>

namespace com
{

    /// @brief Communication runtime singleton
    /// @details 本类不持有成员：进程级状态在 runtime.cpp 的存储里，
    ///          Get() 只返回一个句柄，便于在任意 TU 里使用。
    class Runtime
    {
    public:
        /// @brief Get the global runtime instance (never null)
        static Runtime *Get() noexcept;

        /// @brief 装配并初始化绑定（幂等）
        /// @param[in] applicationName 进程身份（vsomeip 需要），由应用提供
        /// @details 绑定由 com 按编译期选择（COM_WITH_VSOMEIP）装配；失败时状态回滚。
        Result<void> Initialize(std::string const &applicationName) noexcept;

        /// @brief 反初始化全部绑定
        Result<void> Deinit() noexcept;

        /// @brief 启动全部绑定（阻塞）
        void Start() noexcept;

        /// @brief 停止全部绑定（可从其它线程调用来打断 Start）
        void Stop() noexcept;

        /// @brief 是否已 Initialize
        bool IsInitialized() const noexcept;

        // -- P3: InstanceSpecifier → (serviceId, instanceId) mapping --

        void RegisterServiceMapping(
            InstanceSpecifier const &specifier,
            ServiceIdentifier serviceId,
            InstanceIdentifier instanceId) noexcept;

        bool ResolveServiceMapping(
            InstanceSpecifier const &specifier,
            ServiceIdentifier &serviceId,
            InstanceIdentifier &instanceId) const noexcept;

    private:
        Runtime() noexcept;
        ~Runtime() noexcept;

        Runtime(Runtime const &) = delete;
        Runtime &operator=(Runtime const &) = delete;
    };

} // namespace com
