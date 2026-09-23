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
        return Result<void>(true);
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
        return Result<void>(true);
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
            return Result<void>(false, "Invalid bind runtime");
        }

        auto name = bindRuntime->GetName();
        if (!name)
        {
            return Result<void>(false, "Bind runtime has no name");
        }

        // Initialize the bind runtime
        auto result = bindRuntime->Init();
        if (!result)
        {
            return result;
        }

        // Store the bind runtime
        bindRuntimes_[name] = std::move(bindRuntime);
        return Result<void>(true);
    }

    Result<void> Runtime::UnregisterBindRuntime(char const *name) noexcept
    {
        if (!name)
        {
            return Result<void>(false, "Invalid name");
        }

        auto it = bindRuntimes_.find(name);
        if (it == bindRuntimes_.end())
        {
            return Result<void>(false, "Bind runtime not found");
        }

        if (it->second)
        {
            it->second->Deinit();
        }
        bindRuntimes_.erase(it);
        return Result<void>(true);
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
            names.push_back(name);
        }
        return names;
    }

} // namespace com