// Copyright 2024
// Licensed under the Apache License, Version 2.0

/// @file       runtime.cpp
/// @brief      Communication runtime implementation

#include "com/runtime.h"
#include "com/bind_runtime.h"
#include <stdexcept>

namespace com
{

    /// @brief Global runtime instance
    static Runtime *g_runtime = nullptr;

    Runtime::Runtime() noexcept = default;

    Runtime::~Runtime() noexcept
    {
        // Unregister all bind runtimes
        for (auto &[name, rt] : bindRuntimes_)
        {
            if (rt)
            {
                rt->Deinit();
            }
        }
        bindRuntimes_.clear();
    }

    Runtime *Runtime::Get() noexcept
    {
        static Runtime instance;
        g_runtime = &instance;
        return &instance;
    }

    Result<void> Runtime::Init() noexcept
    {
        initialized_ = true;
        return Result<void>::FromValue();
    }

    Result<void> Runtime::Deinit() noexcept
    {
        // Deinit all bind runtimes
        for (auto &[name, rt] : bindRuntimes_)
        {
            if (rt)
            {
                rt->Deinit();
            }
        }
        initialized_ = false;
        return Result<void>::FromValue();
    }

    void Runtime::Start() noexcept
    {
        // Start all bind runtimes
        for (auto &[name, rt] : bindRuntimes_)
        {
            if (rt)
            {
                rt->Start();
            }
        }
    }

    void Runtime::Stop() noexcept
    {
        // Stop all bind runtimes
        for (auto &[name, rt] : bindRuntimes_)
        {
            if (rt)
            {
                rt->Stop();
            }
        }
    }

    Result<void> Runtime::RegisterBindRuntime(std::unique_ptr<BindRuntime> bindRuntime) noexcept
    {
        if (!bindRuntime)
        {
            return Result<void>::FromError(ComErrc::kInvalidArgument);
        }

        auto name = bindRuntime->GetName();
        if (!name)
        {
            return Result<void>::FromError(ComErrc::kInvalidName);
        }

        // Initialize the bind runtime
        auto result = bindRuntime->Init();
        if (!result)
        {
            return result;
        }

        // Store the bind runtime
        bindRuntimes_[name] = std::move(bindRuntime);
        return Result<void>::FromValue();
    }

    Result<void> Runtime::UnregisterBindRuntime(char const *name) noexcept
    {
        if (!name)
        {
            return Result<void>::FromError(ComErrc::kInvalidName);
        }

        auto it = bindRuntimes_.find(name);
        if (it == bindRuntimes_.end())
        {
            return Result<void>::FromError(ComErrc::kNotFound);
        }

        if (it->second)
        {
            it->second->Deinit();
        }
        bindRuntimes_.erase(it);
        return Result<void>::FromValue();
    }

    BindRuntime *Runtime::GetBindRuntime(char const *name) noexcept
    {
        if (!name)
        {
            return nullptr;
        }

        auto it = bindRuntimes_.find(name);
        if (it != bindRuntimes_.end())
        {
            return it->second.get();
        }
        return nullptr;
    }

    std::vector<char const *> Runtime::GetBindRuntimeNames() const noexcept
    {
        std::vector<char const *> names;
        names.reserve(bindRuntimes_.size());
        for (auto const &[name, rt] : bindRuntimes_)
        {
            names.push_back(name.c_str());
        }
        return names;
    }

    // -- P3: InstanceSpecifier → (serviceId, instanceId) mapping --

    void Runtime::RegisterServiceMapping(
        InstanceSpecifier const &specifier,
        ServiceIdentifier serviceId,
        InstanceIdentifier instanceId) noexcept
    {
        auto sv = specifier.ToString();
        serviceMappings_[std::string(sv.data(), sv.size())] = {serviceId, instanceId};
    }

    bool Runtime::ResolveServiceMapping(
        InstanceSpecifier const &specifier,
        ServiceIdentifier &serviceId,
        InstanceIdentifier &instanceId) const noexcept
    {
        auto sv = specifier.ToString();
        auto it = serviceMappings_.find(std::string(sv.data(), sv.size()));
        if (it != serviceMappings_.end())
        {
            serviceId = it->second.serviceId;
            instanceId = it->second.instanceId;
            return true;
        }
        return false;
    }

} // namespace com
