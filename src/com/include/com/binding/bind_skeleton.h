// Copyright 2024
// Licensed under the Apache License, Version 2.0

/// @file       bind_skeleton.h
/// @brief      Binding layer server-side handle (protocol SPI)
/// @details    BindSkeleton 属绑定 SPI，业务层不得直接包含；
///             公共 API 只经 com/api/skeleton.h 暴露 Skeleton / ServiceSkeleton。
///             见 docs/adr/0001-public-api-ownership.md（C2/C13）。

#pragma once

#include "com/api/types.h"
#include <functional>
#include <memory>
#include <set>
#include <vector>

namespace com
{
    namespace skeleton
    {

        /// @brief Binding layer skeleton abstract interface
        /// @details Represents the server-side binding for a service instance.
        ///          Each protocol implementation must provide its own BindSkeleton.
        class BindSkeleton
        {
        public:
            BindSkeleton() noexcept = default;
            virtual ~BindSkeleton() noexcept = default;

            BindSkeleton(BindSkeleton const &) = delete;
            BindSkeleton &operator=(BindSkeleton const &) = delete;

            /// @brief Initialize the bind skeleton
            virtual Result<void> Init() noexcept = 0;

            /// @brief Deinitialize the bind skeleton
            virtual Result<void> Deinit() noexcept = 0;

            /// @brief Offer the service
            virtual Result<void> Offer() noexcept = 0;

            /// @brief Stop offering the service
            virtual Result<void> StopOffer() noexcept = 0;

            /// @brief Register a method handler
            /// @param[in] methodId Method identifier
            /// @param[in] handler Called with (request, response)
            virtual void RegisterMethodHandler(
                MethodIdentifier methodId,
                std::function<void(std::vector<uint8_t> const &, std::vector<uint8_t> &)> handler) noexcept = 0;

            /// @brief Offer an event/field and register it into its event groups
            /// @param[in] eventId Event/field identifier
            /// @param[in] eventGroupIds Event groups the event belongs to
            /// @param[in] type Plain event or field
            virtual Result<void> OfferEvent(
                EventIdentifier eventId,
                std::set<EventGroupIdentifier> const &eventGroupIds,
                EventType type = EventType::kEvent) noexcept = 0;

            /// @brief Stop offering an event/field
            /// @param[in] eventId Event/field identifier
            virtual Result<void> StopOfferEvent(EventIdentifier eventId) noexcept = 0;

            /// @brief Send a notification for an event/field
            /// @param[in] eventId Event/field identifier
            /// @param[in] eventData Serialized event payload
            virtual void SendEvent(
                EventIdentifier eventId,
                std::vector<uint8_t> const &eventData) noexcept = 0;

            /// @brief Get the binding runtime name
            virtual char const *GetBindRuntimeName() const noexcept = 0;
        };

    } // namespace skeleton
} // namespace com
