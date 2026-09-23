// Copyright 2024
// Licensed under the Apache License, Version 2.0

/// @file       main.cpp
/// @brief      SOME/IP Server using decoupled communication layer

#include "com/runtime.h"
#include "com/vsomeip/vsomeip_bind_runtime.h"
#include "com/skeleton.h"

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
class VersionServiceSkeleton : public com::skeleton::ServiceSkeleton<VersionServiceSkeleton>
{
public:
    explicit VersionServiceSkeleton(std::string version) noexcept
        : com::skeleton::ServiceSkeleton<VersionServiceSkeleton>(kServiceId, kInstanceId),
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
    if (!runtime)
    {
        std::cerr << "Failed to get runtime\n";
        return 1;
    }
    g_runtime = runtime;

    // Initialize runtime
    auto initResult = runtime->Init();
    if (!initResult)
    {
        std::cerr << "Failed to initialize runtime\n";
        return 1;
    }

    // Create and register vsomeip bind runtime
    auto vsomeipRuntime = std::make_unique<com::vsomeip_binding::VsomeipBindRuntime>("someip-service");
    auto vsomeipInitResult = vsomeipRuntime->Init();
    if (!vsomeipInitResult)
    {
        std::cerr << "Failed to initialize vsomeip runtime\n";
        return 1;
    }

    auto registerResult = runtime->RegisterBindRuntime(std::move(vsomeipRuntime));
    if (!registerResult)
    {
        std::cerr << "Failed to register vsomeip runtime\n";
        return 1;
    }

    // Get the vsomeip bind runtime
    auto *bindRuntime = runtime->GetBindRuntime("vsomeip");
    if (!bindRuntime)
    {
        std::cerr << "Failed to get vsomeip runtime\n";
        return 1;
    }

    // Create service skeleton
    VersionServiceSkeleton skeleton(version);

    // Create bind skeletons
    std::vector<std::unique_ptr<com::skeleton::BindSkeleton>> bindSkeletons;
    bindRuntime->CreateBindSkeleton(skeleton, 1, bindSkeletons);

    // Offer the service
    for (auto &bs : bindSkeletons)
    {
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
