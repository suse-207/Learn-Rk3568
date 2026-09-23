// Copyright 2024
// Licensed under the Apache License, Version 2.0

/// @file       bind_runtime.h
/// @brief      Binding layer runtime abstract interface
/// @details    Abstract interface for protocol binding implementations (SOMEIP, DDS, DoIP, UDS, vsomeip)

#pragma once

#include "com/types.h"
#include <memory>
#include <vector>

namespace com
{

    // Forward declarations
    namespace proxy
    {
        class BindHandle;
        class BindProxy;
    } // namespace proxy

    namespace skeleton
    {
        class BindSkeleton;
        class Skeleton;
    } // namespace skeleton

    /// @brief Binding layer runtime abstract interface
    /// @details This is the core abstraction that allows protocol-agnostic communication.
    ///          Each protocol implementation (SOMEIP, DDS, DoIP, UDS, vsomeip) must
    ///          inherit from this class and implement the virtual methods.
    ///
    /// Architecture:
    /// @code
    ///   Application Layer (ServiceProxy/ServiceSkeleton)
    ///          |
    ///          v
    ///   Binding Layer (BindHandle/BindProxy/BindSkeleton) <- Abstract
    ///          |
    ///          v
    ///   Runtime Layer (BindRuntime) <- This class, Abstract
    ///          |
    ///          v
    ///   Protocol Implementation (SOMEIP/DDS/DoIP/UDS/vsomeip)
    /// @endcode
    class BindRuntime
    {
    public:
        /// @brief Constructor
        BindRuntime() noexcept = default;

        /// @brief Destructor
        virtual ~BindRuntime() noexcept = default;

        /// @brief Copy constructor
        BindRuntime(BindRuntime const &other) noexcept = default;

        /// @brief Move constructor
        BindRuntime(BindRuntime &&other) noexcept = default;

        /// @brief Copy assignment operator
        BindRuntime &operator=(BindRuntime const &other) noexcept = default;

        /// @brief Move assignment operator
        BindRuntime &operator=(BindRuntime &&other) noexcept = default;

        /// @brief Create a binding layer skeleton instance (Skeleton side)
        /// @param[in] skeleton Service skeleton
        /// @param[in] instanceIdentifier Instance identifier
        /// @param[out] bindSkeletons Set of skeleton instance pointers
        /// @details Called when a service skeleton is initialized to create
        ///          protocol-specific binding skeletons.
        virtual void CreateBindSkeleton(
            skeleton::Skeleton &skeleton,
            InstanceIdentifier const &instanceIdentifier,
            std::vector<std::unique_ptr<skeleton::BindSkeleton>> &bindSkeletons) noexcept = 0;

        /// @brief Get set of available service instance handles (Proxy side)
        /// @param[in] serviceIdentifier Service identifier
        /// @param[in] instanceIdentifier Instance identifier
        /// @param[out] bindHandles Set of available service handles
        /// @details Called for service discovery to get available service instances.
        virtual void GetAvailableServiceHandles(
            ServiceIdentifier const &serviceIdentifier,
            InstanceIdentifier const &instanceIdentifier,
            ServiceHandleContainer<std::shared_ptr<proxy::BindHandle>> &bindHandles) noexcept = 0;

        /// @brief Register a find service handler (Proxy side)
        /// @param[in] findHandle Find service handle
        /// @param[in] findHandler Find service handler
        /// @details Called when starting service discovery.
        virtual void RegisterFindServiceHandle(
            FindServiceHandle const &findHandle,
            FindServiceHandler<std::shared_ptr<proxy::BindHandle>> const &findHandler) noexcept = 0;

        /// @brief Unregister a find service handler (Proxy side)
        /// @param[in] findHandle Find service handle
        /// @details Called when stopping service discovery.
        virtual void UnregisterFindServiceHandle(FindServiceHandle const &findHandle) noexcept = 0;

        /// @brief Get the name of this binding runtime
        /// @return Binding runtime name (e.g., "vsomeip", "nsomeip", "dds", "doip", "uds")
        virtual char const *GetName() const noexcept = 0;
    };

} // namespace com