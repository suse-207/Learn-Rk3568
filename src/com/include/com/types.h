// Copyright 2024
// Licensed under the Apache License, Version 2.0

/// @file       types.h
/// @brief      Communication types definition
/// @details    Common types used across the communication stack.
///             P0: Result now uses ara::core::ErrorCode instead of std::string.
///             P3: InstanceSpecifier alias added.

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <functional>
#include <mutex>
#include <atomic>
#include <chrono>

#include "ara/core/error_code.h"
#include "ara/core/instance_specifier.h"
#include "ara/core/future.h"
#include "ara/core/promise.h"
#include "com/com_error_domain.h"

namespace com
{

    /// @brief Service identifier type
    using ServiceIdentifier = uint16_t;

    /// @brief Instance identifier type
    using InstanceIdentifier = uint16_t;

    /// @brief Method identifier type
    using MethodIdentifier = uint16_t;

    /// @brief Event identifier type
    using EventIdentifier = uint16_t;

    /// @brief Event group identifier type
    using EventGroupIdentifier = uint16_t;

    /// @brief Field identifier type
    using FieldIdentifier = uint16_t;

    /// @brief Instance identifier container type
    using InstanceIdentifierContainer = std::vector<InstanceIdentifier>;

    /// @brief InstanceSpecifier (P3: AUTOSAR-style service identification)
    using InstanceSpecifier = ara::core::InstanceSpecifier;

    /// @brief Future type for async operations (P2: ara::com-style async)
    /// @tparam T Value type
    template <typename T>
    using Future = ara::core::Future<T>;

    /// @brief Promise type for async operations (P2: ara::com-style async)
    /// @tparam T Value type
    template <typename T>
    using Promise = ara::core::Promise<T>;

    /// @brief Duration type for timeouts
    using Duration = std::chrono::milliseconds;

    /// @brief Service handle container type
    template <typename T>
    using ServiceHandleContainer = std::vector<T>;

    /// @brief Find service handle type
    struct FindServiceHandle
    {
        ServiceIdentifier serviceIdentifier;
        InstanceIdentifier instanceIdentifier;
        uint64_t uid;

        FindServiceHandle() : serviceIdentifier(0), instanceIdentifier(0), uid(GenerateUID()) {}
        FindServiceHandle(ServiceIdentifier svc, InstanceIdentifier inst)
            : serviceIdentifier(svc), instanceIdentifier(inst), uid(GenerateUID()) {}

        static uint64_t GenerateUID()
        {
            static std::atomic<uint64_t> counter{0};
            return ++counter;
        }
    };

    /// @brief Find service handler type
    template <typename T>
    using FindServiceHandler = std::function<void(ServiceHandleContainer<T> const &)>;

    /// @brief Method call processing mode
    enum class MethodCallProcessingMode
    {
        kEvent,   ///< Event-driven mode
        kParallel ///< Parallel mode
    };

    // -----------------------------------------------------------------------
    // Result type (P0: uses ara::core::ErrorCode instead of std::string)
    // -----------------------------------------------------------------------

    /// @brief Result type for error handling
    template <typename T>
    class Result
    {
    public:
        Result() : hasValue_(true), value_{}, error_(MakeErrorCode(ComErrc::kSuccess)) {}
        Result(T const &value) : hasValue_(true), value_(value), error_(MakeErrorCode(ComErrc::kSuccess)) {}
        Result(T &&value) : hasValue_(true), value_(std::move(value)), error_(MakeErrorCode(ComErrc::kSuccess)) {}
        explicit Result(ara::core::ErrorCode ec) : hasValue_(false), error_(ec) {}
        Result(bool success, std::string const &errorMsg)
            : hasValue_(success),
              error_(success ? MakeErrorCode(ComErrc::kSuccess)
                             : MakeErrorCode(ComErrc::kInternalError))
        {
            (void)errorMsg;
        }

        static Result FromValue(T const &v) { return Result(v); }
        static Result FromValue(T &&v) { return Result(std::move(v)); }
        static Result FromError(ara::core::ErrorCode ec) { return Result(ec); }
        static Result FromError(ComErrc ec) { return Result(MakeErrorCode(ec)); }

        bool HasValue() const { return hasValue_; }
        T const &Value() const { return value_; }
        T const &value() const { return value_; }
        T &&MoveValue() { return std::move(value_); }
        ara::core::ErrorCode Error() const { return error_; }

        struct ErrorInfo
        {
            ara::core::ErrorCode errorCode;
            std::string message;
        };
        ErrorInfo error() const
        {
            auto message = error_.Message();
            std::string msg(message.data(), message.size());
            return {error_, std::move(msg)};
        }

        operator bool() const { return hasValue_; }

    private:
        bool hasValue_{true};
        T value_{};
        ara::core::ErrorCode error_;
    };

    /// @brief Void result specialization
    template <>
    class Result<void>
    {
    public:
        Result() : hasValue_(true), error_(MakeErrorCode(ComErrc::kSuccess)) {}
        explicit Result(ara::core::ErrorCode ec) : hasValue_(false), error_(ec) {}
        Result(bool success, std::string const &errorMsg)
            : hasValue_(success),
              error_(success ? MakeErrorCode(ComErrc::kSuccess)
                             : MakeErrorCode(ComErrc::kInternalError))
        {
            (void)errorMsg;
        }

        static Result FromValue() { return Result(); }
        static Result FromError(ara::core::ErrorCode ec) { return Result(ec); }
        static Result FromError(ComErrc ec) { return Result(MakeErrorCode(ec)); }

        bool HasValue() const { return hasValue_; }
        ara::core::ErrorCode Error() const { return error_; }

        struct ErrorInfo
        {
            ara::core::ErrorCode errorCode;
            std::string message;
        };
        ErrorInfo error() const
        {
            auto message = error_.Message();
            std::string msg(message.data(), message.size());
            return {error_, std::move(msg)};
        }

        operator bool() const { return hasValue_; }

    private:
        bool hasValue_{false};
        ara::core::ErrorCode error_;
    };

} // namespace com
