// Copyright 2024
// Licensed under the Apache License, Version 2.0

/// @file       runtime.cpp
/// @brief      Communication runtime implementation

#include "com/runtime.h"

namespace com
{

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
        return &instance;
    }

    Result<void> Runtime::Init() noexcept
    {
        return Result<void>::FromValue();
    }

    Result<void> Runtime::Deinit() noexcept
    {
        // Collect under the lock, then Deinit outside it so a blocking or re-entrant
        // Deinit cannot deadlock.
        std::vector<BindRuntime *> runtimes;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            runtimes.reserve(bindRuntimes_.size());
            for (auto &[name, rt] : bindRuntimes_)
            {
                if (rt)
                {
                    runtimes.push_back(rt.get());
                }
            }
        }
        for (auto *rt : runtimes)
        {
            rt->Deinit();
        }
        return Result<void>::FromValue();
    }

    void Runtime::Start() noexcept
    {
        // Start is blocking; collect under the lock and run outside it so Stop() can
        // acquire the lock and interrupt the loop.
        std::vector<BindRuntime *> runtimes;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            runtimes.reserve(bindRuntimes_.size());
            for (auto &[name, rt] : bindRuntimes_)
            {
                if (rt)
                {
                    runtimes.push_back(rt.get());
                }
            }
        }
        for (auto *rt : runtimes)
        {
            rt->Start();
        }
    }

    void Runtime::Stop() noexcept
    {
        std::vector<BindRuntime *> runtimes;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            runtimes.reserve(bindRuntimes_.size());
            for (auto &[name, rt] : bindRuntimes_)
            {
                if (rt)
                {
                    runtimes.push_back(rt.get());
                }
            }
        }
        for (auto *rt : runtimes)
        {
            rt->Stop();
        }
    }

    Result<void> Runtime::RegisterBindRuntime(std::unique_ptr<BindRuntime> bindRuntime) noexcept
    {
        if (!bindRuntime)
        {
            return Result<void>::FromError(ComErrc::kInvalidArgument);
        }

        char const *name = bindRuntime->GetName();
        if (!name)
        {
            return Result<void>::FromError(ComErrc::kInvalidName);
        }
        std::string key(name);

        // Initialize the bind runtime before publishing it.
        auto result = bindRuntime->Init();
        if (!result)
        {
            return result;
        }

        // Store the bind runtime
        std::lock_guard<std::mutex> lock(mutex_);
        bindRuntimes_[std::move(key)] = std::move(bindRuntime);
        return Result<void>::FromValue();
    }

    Result<void> Runtime::UnregisterBindRuntime(std::string const &name) noexcept
    {
        std::lock_guard<std::mutex> lock(mutex_);
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

    BindRuntime *Runtime::GetBindRuntime(std::string const &name) noexcept
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = bindRuntimes_.find(name);
        if (it != bindRuntimes_.end())
        {
            return it->second.get();
        }
        return nullptr;
    }

    std::vector<std::string> Runtime::GetBindRuntimeNames() const noexcept
    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<std::string> names;
        names.reserve(bindRuntimes_.size());
        for (auto const &[name, rt] : bindRuntimes_)
        {
            names.push_back(name);
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
        std::string key(sv.data(), sv.size());
        std::lock_guard<std::mutex> lock(mutex_);
        serviceMappings_[std::move(key)] = {serviceId, instanceId};
    }

    bool Runtime::ResolveServiceMapping(
        InstanceSpecifier const &specifier,
        ServiceIdentifier &serviceId,
        InstanceIdentifier &instanceId) const noexcept
    {
        auto sv = specifier.ToString();
        std::string key(sv.data(), sv.size());
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = serviceMappings_.find(key);
        if (it != serviceMappings_.end())
        {
            serviceId = it->second.serviceId;
            instanceId = it->second.instanceId;
            return true;
        }
        return false;
    }

} // namespace com
