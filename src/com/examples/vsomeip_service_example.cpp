// Copyright 2024
// Licensed under the Apache License, Version 2.0

/// @file       vsomeip_service_example.cpp
/// @brief      Example demonstrating how to use the decoupled communication layer
///             with vsomeip binding to create a SOME/IP service

#include "com/runtime.h"
#include "com/vsomeip/vsomeip_bind_runtime.h"
#include "com/skeleton.h"
#include <iostream>
#include <thread>
#include <chrono>

// Example service skeleton
class ExampleServiceSkeleton : public com::skeleton::ServiceSkeleton<ExampleServiceSkeleton>
{
public:
    ExampleServiceSkeleton() noexcept
        : com::skeleton::ServiceSkeleton<ExampleServiceSkeleton>(0x1234, 1)
    {
    }

    void OnEchoRequest(std::vector<uint8_t> const &request, std::vector<uint8_t> &response)
    {
        std::cout << "Received echo request with " << request.size() << " bytes" << std::endl;
        // Echo back the request data
        response = request;
    }

    void OnGetTemperatureRequest(std::vector<uint8_t> const &request, std::vector<uint8_t> &response)
    {
        std::cout << "Received temperature request" << std::endl;
        // Return a mock temperature value (25.5 degrees)
        float temperature = 25.5f;
        response.resize(sizeof(float));
        std::memcpy(response.data(), &temperature, sizeof(float));
    }
};

int main(int argc, char *argv[])
{
    std::cout << "CAPI Communication Layer - vsomeip Service Example" << std::endl;

    // Get the runtime instance
    auto *runtime = com::Runtime::Get();
    if (!runtime)
    {
        std::cerr << "Failed to get runtime" << std::endl;
        return 1;
    }

    // Initialize runtime
    auto initResult = runtime->Init();
    if (!initResult)
    {
        std::cerr << "Failed to initialize runtime" << std::endl;
        return 1;
    }

    // Create vsomeip bind runtime
    auto vsomeipRuntime = std::make_unique<com::vsomeip_binding::VsomeipBindRuntime>("example_service");
    auto vsomeipInitResult = vsomeipRuntime->Init();
    if (!vsomeipInitResult)
    {
        std::cerr << "Failed to initialize vsomeip runtime" << std::endl;
        return 1;
    }

    // Register vsomeip bind runtime
    auto registerResult = runtime->RegisterBindRuntime(std::move(vsomeipRuntime));
    if (!registerResult)
    {
        std::cerr << "Failed to register vsomeip runtime" << std::endl;
        return 1;
    }

    // Create service skeleton
    ExampleServiceSkeleton skeleton;

    // Create bind skeleton for vsomeip
    std::vector<std::unique_ptr<com::skeleton::BindSkeleton>> bindSkeletons;
    auto *bindRuntime = runtime->GetBindRuntime("vsomeip");
    if (bindRuntime)
    {
        bindRuntime->CreateBindSkeleton(skeleton, 1, bindSkeletons);
    }

    // Offer the service
    for (auto &bs : bindSkeletons)
    {
        bs->Offer();
    }

    std::cout << "Service offered. Press Ctrl+C to exit." << std::endl;

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