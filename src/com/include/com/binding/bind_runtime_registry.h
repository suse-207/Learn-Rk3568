// Copyright 2024
// Licensed under the Apache License, Version 2.0

/// @file       bind_runtime_registry.h
/// @brief      绑定 runtime 注册表（绑定层内部 API）
/// @details    只允许 com 内部实现与绑定层测试/示例包含。应用应使用
///             com::Runtime::Initialize() 与 com::proxy::ServiceProxy。
///             见 docs/adr/0001-public-api-ownership.md（C19）。

#pragma once

#include "com/binding/bind_runtime.h"
#include <memory>
#include <string>
#include <vector>

namespace com
{
    namespace binding
    {

        /// @brief 已装配的绑定名（按名字有序）
        std::vector<std::string> BindRuntimeNames() noexcept;

        /// @brief 按名字取绑定
        BindRuntime *FindBindRuntime(std::string const &name) noexcept;

        /// @brief 装配一个绑定：内部先 Init()，成功后才对外可见
        Result<void> AttachBindRuntime(std::unique_ptr<BindRuntime> bindRuntime) noexcept;

        /// @brief 卸载并 Deinit 一个绑定
        Result<void> DetachBindRuntime(std::string const &name) noexcept;

        /// @brief 过渡用：取第一个已装配的绑定
        /// @details R2 起应用改用 ServiceProxy::StartFindService / FindService，
        ///          本函数届时删除。
        BindRuntime *FirstBindRuntime() noexcept;

    } // namespace binding
} // namespace com
