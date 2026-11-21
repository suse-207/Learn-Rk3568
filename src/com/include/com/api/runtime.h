// Copyright 2024
// Licensed under the Apache License, Version 2.0

/// @file       runtime.h
/// @brief      Communication runtime - manages binding layer instances
/// @details    Singleton that manages multiple BindRuntime implementations.
///             P3: Added InstanceSpecifier → (serviceId, instanceId) mapping.

#pragma once

#include "com/api/types.h"
#include "com/binding/bind_runtime.h"
#include <memory>
#include <vector>
#include <string>
#include <map>
#include <mutex>

namespace com
{

    /// @brief Communication runtime singleton
    /// @details Manages multiple binding layer runtime instances.
    ///          Supports registering multiple protocol implementations (vsomeip, dds, etc.)
    ///          P3: Provides InstanceSpecifier → numeric ID resolution.
    class Runtime
    {
    public:
        /// @brief Get the global runtime instance (never null)
        static Runtime *Get() noexcept;

        /// @brief Initialize the runtime
        Result<void> Init() noexcept;

        /// @brief Deinitialize the runtime
        Result<void> Deinit() noexcept;

        /// @brief Start all registered bind runtimes (blocking)
        void Start() noexcept;

        /// @brief Stop all registered bind runtimes
        void Stop() noexcept;

        /// @brief Binding runtime unique pointer type
        using BindRuntimePtr = std::unique_ptr<BindRuntime>;

        /// @brief Register a binding layer runtime instance
        // 注册前会先调用 bindRuntime->Init()，只有 Init() 成功后才把该 runtime 放入内部映射。
        // 因此 Init() 失败即注册失败：错误码原样返回，runtime 不会进入映射，也不会被后续查找命中。
        // 调用方须先确保依赖已就绪（例如 vsomeip 运行时、应用名等），否则 Init() 会失败。
        // 调用方须检查返回值，不要假定注册一定成功。
        Result<void> RegisterBindRuntime(std::unique_ptr<BindRuntime> bindRuntime) noexcept;

        /// @brief Unregister a binding layer runtime by name
        Result<void> UnregisterBindRuntime(std::string const &name) noexcept;

        /// @brief Get a binding layer runtime by name
        /// @return Pointer into the runtime, valid until the next registration change.
        BindRuntime *GetBindRuntime(std::string const &name) noexcept;

        /// @brief Get all registered bind runtime names
        std::vector<std::string> GetBindRuntimeNames() const noexcept;

        // -- P3: InstanceSpecifier → (serviceId, instanceId) mapping --

        /// @brief Register a mapping from InstanceSpecifier to numeric IDs
        void RegisterServiceMapping(
            InstanceSpecifier const &specifier,
            ServiceIdentifier serviceId,
            InstanceIdentifier instanceId) noexcept;

        /// @brief Resolve an InstanceSpecifier to numeric IDs
        /// @return true if found, false otherwise
        bool ResolveServiceMapping(
            InstanceSpecifier const &specifier,
            ServiceIdentifier &serviceId,
            InstanceIdentifier &instanceId) const noexcept;

    private:
        Runtime() noexcept;
        ~Runtime() noexcept;

        Runtime(Runtime const &) = delete;
        Runtime &operator=(Runtime const &) = delete;

        mutable std::mutex mutex_;
        bool initialized_{false};
        std::map<std::string, std::unique_ptr<BindRuntime>> bindRuntimes_;

        // P3: InstanceSpecifier → (serviceId, instanceId) mapping
        struct ServiceMapping
        {
            ServiceIdentifier serviceId;
            InstanceIdentifier instanceId;
        };
        std::map<std::string, ServiceMapping> serviceMappings_;
    };

} // namespace com