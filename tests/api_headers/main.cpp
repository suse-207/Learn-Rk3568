// Copyright 2024
// Licensed under the Apache License, Version 2.0

/// @file       main.cpp
/// @brief      公共 API 头卫生守卫（ADR 0001 / C2 / C18）
/// @details    本 TU 只包含 com/api/**。任何"公共头偷偷依赖绑定类型"的回归
///             （例如 api/proxy.h 又 include 了 binding/bind_handle.h 并因此
///             要求 binding 目录可见）都会让它编译失败。
///             反向检查（api/ 里不得出现绑定标识符）见 check_api_headers.sh。

#include "com/api/types.h"
#include "com/api/com_error_domain.h"
#include "com/api/runtime.h"
#include "com/api/proxy.h"
#include "com/api/skeleton.h"

int main()
{
    // 只为让头文件里的类型被真正用到（避免"只是没展开"的假通过）
    com::ServiceIdentifier serviceId = 0x6100;
    com::MethodIdentifier methodId = 0x0001;
    com::ComErrc errc = com::ComErrc::kSuccess;
    com::proxy::ServiceProxy *proxy = nullptr;
    com::skeleton::Skeleton *skeleton = nullptr;
    (void)serviceId;
    (void)methodId;
    (void)errc;
    (void)proxy;
    (void)skeleton;
    return 0;
}
