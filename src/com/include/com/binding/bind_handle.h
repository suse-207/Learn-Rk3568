// Copyright 2024
// Licensed under the Apache License, Version 2.0

/// @file       bind_handle.h
/// @brief      Binding layer client-side handles (protocol SPI)
/// @details    BindHandle / BindProxy 属绑定 SPI，业务层不得直接包含；
///             公共 API 只经 com/api/proxy.h 暴露 ServiceProxy。
///             见 docs/adr/0001-public-api-ownership.md（C2）。

#pragma once

#include "com/api/types.h"
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace com
{
    namespace proxy
    {

        /// @brief Binding layer handle abstract interface
        /// @details Represents a connection to a specific service instance.
        ///          Each protocol implementation must provide its own BindHandle.
        class BindHandle
        {
        public:
            BindHandle() noexcept = default;
            virtual ~BindHandle() noexcept = default;

            BindHandle(BindHandle const &) = delete;
            BindHandle &operator=(BindHandle const &) = delete;

            /// @brief Check if handle is valid
            virtual bool IsValid() const noexcept = 0;

            /// @brief Get the binding runtime name
            virtual char const *GetBindRuntimeName() const noexcept = 0;

            /// @brief Send a request and wait for the response
            virtual Result<std::vector<uint8_t>> SendRequest(
                MethodIdentifier methodId,
                std::vector<uint8_t> const &requestData,
                Duration timeout) noexcept = 0;

            /// @brief Send a request asynchronously, returning a Future (P2: ara::com-style)
            /// @details 不承诺超时：只在响应到达或 Stop() 时结算（com 不拥有 loop，
            ///          没有线程能推进 deadline）
            virtual Future<std::vector<uint8_t>> SendRequestAsync(
                MethodIdentifier methodId,
                std::vector<uint8_t> const &requestData) noexcept = 0;

            /// @brief Subscribe to an event group and register a notification handler
            /// @param[in] eventId Event identifier
            /// @param[in] eventGroupId Event group the event belongs to
            /// @param[in] handler Called with the serialized event payload
            virtual void SubscribeEvent(
                EventIdentifier eventId,
                EventGroupIdentifier eventGroupId,
                std::function<void(std::vector<uint8_t> const &)> handler) noexcept = 0;

            /// @brief Unsubscribe from an event group
            /// @param[in] eventId Event identifier
            /// @param[in] eventGroupId Event group the event belongs to
            virtual void UnsubscribeEvent(
                EventIdentifier eventId,
                EventGroupIdentifier eventGroupId) noexcept = 0;
        };

        /// @brief Binding layer proxy abstract interface
        /// @details Base class for protocol-specific proxy implementations.
        class BindProxy
        {
        public:
            BindProxy() noexcept = default;
            virtual ~BindProxy() noexcept = default;

            BindProxy(BindProxy const &) = delete;
            BindProxy &operator=(BindProxy const &) = delete;

            /// @brief Initialize the proxy
            virtual Result<void> Init() noexcept = 0;

            /// @brief Deinitialize the proxy
            virtual Result<void> Deinit() noexcept = 0;

            /// @brief Get the binding runtime name
            virtual char const *GetBindRuntimeName() const noexcept = 0;
        };

    } // namespace proxy
} // namespace com
