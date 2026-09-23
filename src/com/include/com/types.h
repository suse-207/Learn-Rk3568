// Copyright 2024
// Licensed under the Apache License, Version 2.0

/// @file       types.h
/// @brief      Communication types definition
/// @details    Common types used across the communication stack

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

    /// @brief Field identifier type
    using FieldIdentifier = uint16_t;

    /// @brief Instance identifier container type
    using InstanceIdentifierContainer = std::vector<InstanceIdentifier>;

    /// @brief Service handle container type
    template <typename T>
    using ServiceHandleContainer = std::vector<T>;

    /// @brief Find service handle type
    struct FindServiceHandle
    {
        ServiceIdentifier serviceIdentifier;
        InstanceIdentifier instanceIdentifier;
        uint64_t uid;

        static uint64_t GenerateUID()
        {
            static std::atomic<uint64_t> counter{0};
            return ++counter;
        }
    };

    /// @brief Find service handler type
    template <typename T>
    using FindServiceHandler = std::function<void(ServiceHandleContainer<T> const &, FindServiceHandle const &)>;

    /// @brief Method call processing mode
    enum class MethodCallProcessingMode
    {
        kEvent,   ///< Event-driven mode
        kParallel ///< Parallel mode
    };

    /// @brief Result type for error handling
    template <typename T>
    class Result
    {
    public:
        Result() : hasValue_(true), value_{} {}
        Result(T const &value) : hasValue_(true), value_(value) {}
        Result(T &&value) : hasValue_(true), value_(std::move(value)) {}

        static Result Error(std::string const &errorMsg)
        {
            Result r;
            r.hasValue_ = false;
            r.errorMsg_ = errorMsg;
            return r;
        }

        bool HasValue() const { return hasValue_; }
        T const &Value() const { return value_; }
        T &&MoveValue() { return std::move(value_); }
        std::string const &ErrorMsg() const { return errorMsg_; }

        operator bool() const { return hasValue_; }

    private:
        bool hasValue_;
        T value_{};
        std::string errorMsg_;
    };

    /// @brief Void result specialization
    template <>
    class Result<void>
    {
    public:
        Result() : hasValue_(true) {}

        static Result Error(std::string const &errorMsg)
        {
            Result r;
            r.hasValue_ = false;
            r.errorMsg_ = errorMsg;
            return r;
        }

        bool HasValue() const { return hasValue_; }
        std::string const &ErrorMsg() const { return errorMsg_; }

        operator bool() const { return hasValue_; }

    private:
        bool hasValue_;
        std::string errorMsg_;
    };

} // namespace com