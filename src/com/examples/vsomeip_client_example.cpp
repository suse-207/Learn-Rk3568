// Copyright 2024
// Licensed under the Apache License, Version 2.0

/// @file       vsomeip_client_example.cpp
/// @brief      Example demonstrating how to use the decoupled communication layer
///             with vsomeip binding to create a SOME/IP client

#include "com/runtime.h"
#include "com/vsomeip/vsomeip_bind_runtime.h"
#include "com/proxy.h"
#include <iostream>
#include <thread>
#include <chrono>

int main(int argc, char *argv[])
{
    std::cout << "CAPI Communication Layer - vsomeip Client Example" << std::endl;

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
    auto vsomeipRuntime = std::make_unique<com::vsomeip_binding::VsomeipBindRuntime>("example_client");
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

    // Get the vsomeip bind runtime
    auto *bindRuntime = runtime->GetBindRuntime("vsomeip");
    if (!bindRuntime)
    {
        std::cerr << "Failed to get vsomeip runtime" << std::endl;
        return 1;
    }

    // Service identifier (must match the service)
    com::ServiceIdentifier serviceId = 0x1234;
    com::InstanceIdentifier instanceId = 1;

    // Register for service availability
    com::FindServiceHandle findHandle{serviceId, instanceId};
    bindRuntime->RegisterFindServiceHandle(
        findHandle,
        [](com::ServiceHandleContainer<std::shared_ptr<com::proxy::BindHandle>> const &handles)
        {
            std::cout << "Service became available with " << handles.size() << " handles" << std::endl;

            for (auto const &handle : handles)
            {
                if (handle && handle->IsValid())
                {
                    // Send echo request
                    std::vector<uint8_t> requestData = {0x01, 0x02, 0x03, 0x04};
                    auto result = handle->SendRequest(0x0001, requestData, std::chrono::seconds(5));
                    if (result)
                    {
                        std::cout << "Echo response received with " << result.value().size() << " bytes" << std::endl;
                    }
                    else
                    {
                        std::cerr << "Echo request failed: " << result.error().message << std::endl;
                    }

                    // Send temperature request
                    std::vector<uint8_t> tempRequest;
                    auto tempResult = handle->SendRequest(0x0002, tempRequest, std::chrono::seconds(5));
                    if (tempResult && tempResult.value().size() >= sizeof(float))
                    {
                        float temperature;
                        std::memcpy(&temperature, tempResult.value().data(), sizeof(float));
                        std::cout << "Temperature: " << temperature << " degrees" << std::endl;
                    }
                }
            }
        });

    std::cout << "Waiting for service..." << std::endl;

    // Start the runtime (will call the availability handler when service is found)
    std::thread runtimeThread([runtime]()
                              { runtime->Start(); });

    // Run for 10 seconds
    std::this_thread::sleep_for(std::chrono::seconds(10));

    // Cleanup
    bindRuntime->UnregisterFindServiceHandle(findHandle);
    runtime->Stop();
    runtimeThread.join();
    runtime->Deinit();

    return 0;
}