// Copyright 2024
// Licensed under the Apache License, Version 2.0

/// @file       runtime.h
/// @brief      Communication runtime - manages binding layer instances
/// @details    Singleton that manages multiple BindRuntime implementations

#pragma once

#include "com/bind_runtime.h"
#include <memory>
#include <vector>
#include <string>
#include <mutex>

namespace com
{

    /// @brief Communication runtime singleton
    /// @details Manages multiple binding layer runtime instances.
    ///          Supports registering multiple protocol implementations (vsomeip, dds, etc.)
    class Runtime
    {
    public:
        /// @brief Get the singleton instance
        static Runtime &GetInstance() noexcept;

        /// @brief Initialize communication
        static Result<void> Initialize() noexcept;

        /// @brief Deinitialize communication
        static Result<void> Deinitialize() noexcept;

        /// @brief Binding runtime unique pointer type
        using BindRuntimePtr = std::unique_ptr<BindRuntime>;

        /// @brief Binding runtime collection type
        using BindRuntimes = std::vector<BindRuntimePtr>;

        /// @brief Register a binding layer runtime instance
        /// @param[in] bindRuntime Binding layer runtime instance
        /// @details Call this at startup to register protocol implementations.
        ///          Multiple implementations can be registered for multi-protocol support.
        void RegisterBindRuntime(BindRuntimePtr &&bindRuntime) noexcept;

        /// @brief Get the set of binding layer runtime instances
        BindRuntimes const &GetBindRuntimes() const noexcept;

        /// @brief Get process name
        std::string const &GetProcessName() const noexcept;

        /// @brief Set process name
        void SetProcessName(std::string const &name) noexcept;

        /// @brief Resolve instance identifiers from instance specifier string
        /// @param[in] instanceSpecifier Instance specifier string (e.g., "/instance/my_service")
        /// @return Instance identifier container
        static Result<InstanceIdentifierContainer> ResolveInstanceIDs(
            std::string const &instanceSpecifier) noexcept;

    private:
        Runtime() noexcept;
        ~Runtime() noexcept;

        Runtime(Runtime const &) = delete;
        Runtime &operator=(Runtime const &) = delete;

        class Impl;
        std::unique_ptr<Impl> impl_;
    };

} // namespace com