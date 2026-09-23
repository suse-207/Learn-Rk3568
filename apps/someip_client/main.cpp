// Copyright 2024
// Licensed under the Apache License, Version 2.0

/// @file       main.cpp
/// @brief      SOME/IP Client using decoupled communication layer

#include "com/runtime.h"
#include "com/vsomeip/vsomeip_bind_runtime.h"
#include "com/proxy.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <thread>

namespace
{

    constexpr com::ServiceIdentifier kServiceId = 0x1234;
    constexpr com::InstanceIdentifier kInstanceId = 0x5678;
    constexpr com::MethodIdentifier kMethodId = 0x0001;

    std::atomic<bool> g_running{true};
    com::Runtime *g_runtime = nullptr;

} // namespace

void handle_signal(int)
{
    g_running = false;
    if (g_runtime != nullptr)
    {
        g_runtime->Stop();
    }
}

int main()
{
    std::cout << "SOME/IP Client using decoupled communication layer\n";

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
    auto vsomeipRuntime = std::make_unique<com::vsomeip_binding::VsomeipBindRuntime>("someip-client");
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

    // Register for service availability
    com::FindServiceHandle findHandle{kServiceId, kInstanceId};
    std::atomic<bool> requestSent{false};

    bindRuntime->RegisterFindServiceHandle(
        findHandle,
        [&requestSent, bindRuntime, &findHandle](
            com::ServiceHandleContainer<std::shared_ptr<com::proxy::BindHandle>> const &handles)
        {
            std::cout << "Service became available with " << handles.size() << " handles\n";

            for (auto const &handle : handles)
            {
                if (handle && handle->IsValid() && !requestSent.exchange(true))
                {
                    // Send request
                    std::vector<uint8_t> requestData = {'H', 'e', 'l', 'l', 'o'};
                    auto result = handle->SendRequest(kMethodId, requestData, std::chrono::seconds(5));
                    if (result)
                    {
                        std::cout << "Response received with " << result.value().size() << " bytes\n";
                        std::string response(result.value().begin(), result.value().end());
                        std::cout << "Response: " << response << "\n";
                    }
                    else
                    {
                        std::cerr << "Request failed: " << result.error().message << "\n";
                    }

                    // Unregister and stop after receiving response
                    std::thread([bindRuntime, &findHandle]()
                                {
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                        bindRuntime->UnregisterFindServiceHandle(findHandle);
                        if (g_runtime) {
                            g_runtime->Stop();
                        } })
                        .detach();
                }
            }
        });

    // Register signal handlers
    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    std::cout << "Waiting for service...\n";

    // Start the runtime (blocking)
    runtime->Start();

    // Cleanup
    runtime->Deinit();

    return 0;
}
