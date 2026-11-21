// Copyright 2024
// Licensed under the Apache License, Version 2.0

/// @file       main.cpp
/// @brief      SOME/IP Server using decoupled communication layer

#include "com/api/runtime.h"
#include "com/binding/bind_runtime_registry.h"
#include "com/api/skeleton.h"
#include "com/binding/bind_skeleton.h"  // 绑定 SPI：P7 改用 Skeleton 公共 API 后删除

#include <csignal>
#include <chrono>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace
{

    constexpr com::ServiceIdentifier kServiceId = 0x1234;
    constexpr com::InstanceIdentifier kInstanceId = 0x5678;
    constexpr com::MethodIdentifier kMethodId = 0x0001;
    constexpr com::EventIdentifier kEventId = 0x8778;
    constexpr com::EventGroupIdentifier kEventGroupId = 0x4465;

    com::Runtime *g_runtime = nullptr;

} // namespace

/// @brief Service skeleton for version service
class VersionServiceSkeleton : public com::skeleton::ServiceSkeleton
{
public:
    explicit VersionServiceSkeleton(std::string version) noexcept
        : com::skeleton::ServiceSkeleton(kServiceId, kInstanceId),
          version_(std::move(version))
    {
    }

    /// @brief Handle get_version request
    void OnGetVersionRequest(std::vector<uint8_t> const & /*request*/, std::vector<uint8_t> &response)
    {
        std::cout << "someip-service recv method=0x" << std::hex << kMethodId << std::dec << "\n";
        response.assign(version_.begin(), version_.end());
    }

    /// @brief Get the version string
    std::string const &GetVersion() const noexcept
    {
        return version_;
    }

private:
    std::string version_;
};

void handle_signal(int)
{
    if (g_runtime != nullptr)
    {
        g_runtime->Stop();
    }
}

int main(int argc, char **argv)
{
    std::string version = "1.0.0";
    if (argc > 1)
    {
        version = argv[1];
    }

    std::cout << "SOME/IP Server using decoupled communication layer\n";
    std::cout << "Version: " << version << "\n";

    // Get runtime instance
    auto *runtime = com::Runtime::Get();
    if (!runtime) {
        std::cerr << "get runtime failed\n";
        return 1;
    }
    g_runtime = runtime;
    // 装配并初始化绑定：应用不认识具体协议（ADR 0001 / C19）
    auto initResult = runtime->Initialize("someip-service");
    if (!initResult) {
        std::cerr << "initialize runtime failed\n";
        return 1;
    }
    // TODO(R2)：改用 ServiceProxy::FindService/StartFindService 后删除
    auto *bindRuntime = com::binding::FirstBindRuntime();
    if (!bindRuntime)
    {
        std::cerr << "Failed to get bind runtime\n";
        return 1;
    }

    // Create service skeleton
    VersionServiceSkeleton skeleton(version);

    // Create bind skeletons
    std::vector<std::unique_ptr<com::skeleton::BindSkeleton>> bindSkeletons;
    bindRuntime->CreateBindSkeleton(skeleton, kInstanceId, bindSkeletons);

    // Register method handlers and offer the service
    for (auto &bs : bindSkeletons)
    {
        bs->RegisterMethodHandler(kMethodId,
            [&skeleton](std::vector<uint8_t> const &request, std::vector<uint8_t> &response)
            {
                skeleton.OnGetVersionRequest(request, response);
            });
        bs->Offer();
    }

    std::cout << "Service offered. Press Ctrl+C to exit.\n";

    // Register signal handlers
    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    // Start the runtime (blocking)
    runtime->Start();

    // Cleanup
    for (auto &bs : bindSkeletons)
    {
        bs->StopOffer();
    }

    runtime->Stop();
    runtime->Deinit();

    return 0;
}
