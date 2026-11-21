// Copyright 2024
// Licensed under the Apache License, Version 2.0

/// @file       runtime.cpp
/// @brief      Communication runtime 实现 + 绑定注册表实现
/// @details    进程级状态集中在这里：绑定表、初始化标志、InstanceSpecifier 映射。
///             绑定 runtime 的类型只出现在本 TU 与绑定层，公共头看不到。

#include "com/api/runtime.h"

#include "com/binding/bind_runtime.h"
#include "com/binding/bind_runtime_factory.h"
#include "com/binding/bind_runtime_registry.h"

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace com
{
    namespace
    {
        /// @brief InstanceSpecifier → (serviceId, instanceId)
        struct ServiceMapping
        {
            ServiceIdentifier serviceId{0};
            InstanceIdentifier instanceId{0};
        };

        /// @brief 进程级运行时存储
        struct RuntimeStore
        {
            mutable std::mutex mutex;
            bool initialized{false};
            std::map<std::string, std::unique_ptr<BindRuntime>> runtimes;
            std::map<std::string, ServiceMapping> serviceMappings;

            static RuntimeStore &Instance() noexcept
            {
                static RuntimeStore store;
                return store;
            }
        };

        /// @brief 编译期选定的绑定
        constexpr char const *kDefaultBindingName =
#if defined(COM_WITH_VSOMEIP)
            "vsomeip";
#else
            nullptr;
#endif

        /// @brief 在锁外收集绑定指针，避免持锁调用可能阻塞/重入的方法
        std::vector<BindRuntime *> SnapshotRuntimes() noexcept
        {
            auto &store = RuntimeStore::Instance();
            std::vector<BindRuntime *> runtimes;
            std::lock_guard<std::mutex> lock(store.mutex);
            runtimes.reserve(store.runtimes.size());
            for (auto &entry : store.runtimes)
            {
                if (entry.second)
                {
                    runtimes.push_back(entry.second.get());
                }
            }
            return runtimes;
        }
    } // namespace

    // ------------------------------------------------------------------
    // 绑定注册表（绑定层内部 API）
    // ------------------------------------------------------------------

    namespace binding
    {
        std::vector<std::string> BindRuntimeNames() noexcept
        {
            auto &store = RuntimeStore::Instance();
            std::vector<std::string> names;
            std::lock_guard<std::mutex> lock(store.mutex);
            names.reserve(store.runtimes.size());
            for (auto const &entry : store.runtimes)
            {
                names.push_back(entry.first);
            }
            return names;
        }

        BindRuntime *FindBindRuntime(std::string const &name) noexcept
        {
            auto &store = RuntimeStore::Instance();
            std::lock_guard<std::mutex> lock(store.mutex);
            auto it = store.runtimes.find(name);
            return it == store.runtimes.end() ? nullptr : it->second.get();
        }

        Result<void> AttachBindRuntime(std::unique_ptr<BindRuntime> bindRuntime) noexcept
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
            const std::string key(name);

            {
                auto &store = RuntimeStore::Instance();
                std::lock_guard<std::mutex> lock(store.mutex);
                if (!store.initialized)
                {
                    return Result<void>::FromError(ComErrc::kNotInitialized);
                }
                // 重名必须在 Init() 之前拒绝：否则会白白创建一个 vsomeip application
                // 再被覆盖丢掉（静默泄漏 + 第二个绑定失效）
                if (store.runtimes.find(key) != store.runtimes.end())
                {
                    return Result<void>::FromError(ComErrc::kAlreadyInitialized);
                }
            }

            auto result = bindRuntime->Init();
            if (!result)
            {
                return Result<void>::FromError(result.Error());
            }

            auto &store = RuntimeStore::Instance();
            std::lock_guard<std::mutex> lock(store.mutex);
            if (!store.runtimes.emplace(key, std::move(bindRuntime)).second)
            {
                return Result<void>::FromError(ComErrc::kAlreadyInitialized);
            }
            return Result<void>::FromValue();
        }

        Result<void> DetachBindRuntime(std::string const &name) noexcept
        {
            std::unique_ptr<BindRuntime> detached;
            {
                auto &store = RuntimeStore::Instance();
                std::lock_guard<std::mutex> lock(store.mutex);
                auto it = store.runtimes.find(name);
                if (it == store.runtimes.end())
                {
                    return Result<void>::FromError(ComErrc::kNotFound);
                }
                detached = std::move(it->second);
                store.runtimes.erase(it);
            }
            if (detached)
            {
                detached->Deinit();
            }
            return Result<void>::FromValue();
        }

        BindRuntime *FirstBindRuntime() noexcept
        {
            auto &store = RuntimeStore::Instance();
            std::lock_guard<std::mutex> lock(store.mutex);
            if (store.runtimes.empty())
            {
                return nullptr;
            }
            return store.runtimes.begin()->second.get();
        }
    } // namespace binding

    // ------------------------------------------------------------------
    // Runtime
    // ------------------------------------------------------------------

    Runtime::Runtime() noexcept = default;

    Runtime::~Runtime() noexcept
    {
        auto runtimes = SnapshotRuntimes();
        for (auto *rt : runtimes)
        {
            if (rt)
            {
                rt->Deinit();
            }
        }
        auto &store = RuntimeStore::Instance();
        std::lock_guard<std::mutex> lock(store.mutex);
        store.runtimes.clear();
    }

    Runtime *Runtime::Get() noexcept
    {
        static Runtime instance;
        return &instance;
    }

    Result<void> Runtime::Initialize(std::string const &applicationName) noexcept
    {
        auto &store = RuntimeStore::Instance();
        {
            std::lock_guard<std::mutex> lock(store.mutex);
            if (store.initialized)
            {
                return Result<void>::FromValue();  // 幂等
            }
            store.initialized = true;
        }

        if (kDefaultBindingName == nullptr)
        {
            std::lock_guard<std::mutex> lock(store.mutex);
            store.initialized = false;
            return Result<void>::FromError(ComErrc::kNotInitialized);
        }

        // 应用不认识协议名：这里由编译期选择 + 应用名装配
        auto bindRuntime = CreateBindRuntime(kDefaultBindingName, applicationName);
        auto attached = binding::AttachBindRuntime(std::move(bindRuntime));
        if (!attached)
        {
            std::lock_guard<std::mutex> lock(store.mutex);
            store.initialized = false;
            return Result<void>::FromError(attached.Error());
        }
        return Result<void>::FromValue();
    }

    Result<void> Runtime::Deinit() noexcept
    {
        auto runtimes = SnapshotRuntimes();
        for (auto *rt : runtimes)
        {
            if (rt)
            {
                rt->Deinit();
            }
        }
        return Result<void>::FromValue();
    }

    void Runtime::Start() noexcept
    {
        auto runtimes = SnapshotRuntimes();
        for (auto *rt : runtimes)
        {
            if (rt)
            {
                rt->Start();
            }
        }
    }

    void Runtime::Stop() noexcept
    {
        auto runtimes = SnapshotRuntimes();
        for (auto *rt : runtimes)
        {
            if (rt)
            {
                rt->Stop();
            }
        }
    }

    bool Runtime::IsInitialized() const noexcept
    {
        auto &store = RuntimeStore::Instance();
        std::lock_guard<std::mutex> lock(store.mutex);
        return store.initialized;
    }

    void Runtime::RegisterServiceMapping(
        InstanceSpecifier const &specifier,
        ServiceIdentifier serviceId,
        InstanceIdentifier instanceId) noexcept
    {
        auto sv = specifier.ToString();
        std::string key(sv.data(), sv.size());
        auto &store = RuntimeStore::Instance();
        std::lock_guard<std::mutex> lock(store.mutex);
        store.serviceMappings[std::move(key)] = ServiceMapping{serviceId, instanceId};
    }

    bool Runtime::ResolveServiceMapping(
        InstanceSpecifier const &specifier,
        ServiceIdentifier &serviceId,
        InstanceIdentifier &instanceId) const noexcept
    {
        auto sv = specifier.ToString();
        std::string key(sv.data(), sv.size());
        auto &store = RuntimeStore::Instance();
        std::lock_guard<std::mutex> lock(store.mutex);
        auto it = store.serviceMappings.find(key);
        if (it == store.serviceMappings.end())
        {
            return false;
        }
        serviceId = it->second.serviceId;
        instanceId = it->second.instanceId;
        return true;
    }

} // namespace com
