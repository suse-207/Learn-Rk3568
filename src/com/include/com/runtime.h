// Copyright 2024
// Licensed under the Apache License, Version 2.0

/// @file       runtime.h
/// @brief      Communication runtime - manages binding layer instances
/// @details    Singleton that manages multiple BindRuntime implementations.
///             P3: Added InstanceSpecifier → (serviceId, instanceId) mapping.

#pragma once

#include "com/types.h"
#include "com/bind_runtime.h"
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