// Copyright 2024
// Licensed under the Apache License, Version 2.0

/// @file       com_error_domain.h
/// @brief      Communication error domain and error codes
/// @details    Defines the error domain and error codes for the communication layer,
///             following the AUTOSAR ara::core::ErrorDomain pattern.

#pragma once

#include "ara/core/error_code.h"
#include "ara/core/error_domain.h"
#include "ara/core/exception.h"

namespace com
{

    /// @brief Communication error codes
    /// @details Error codes that can occur in the communication layer.
    ///          Each error code belongs to the ComErrorDomain.
    enum class ComErrc : ara::core::ErrorDomain::CodeType
    {
        kSuccess             = 0,   ///< Success (no error)
        kServiceNotFound     = 1,   ///< Service not found / not available
        kTimeout             = 2,   ///< Operation timed out
        kSerializationFailed = 3,   ///< Serialization / deserialization failed
        kInvalidHandle       = 4,   ///< Invalid communication handle
        kInvalidArgument     = 5,   ///< Invalid argument
        kNotInitialized      = 6,   ///< Component not initialized
        kAlreadyInitialized  = 7,   ///< Component already initialized
        kCommunicationError  = 8,   ///< Generic communication error
        kInternalError       = 9,   ///< Internal error
        kInvalidName         = 10,  ///< Invalid runtime name
        kNotFound            = 11,  ///< Entity not found
    };

    /// @brief Exception type for communication errors
    class ComException : public ara::core::Exception
    {
    public:
        explicit ComException(ara::core::ErrorCode err) noexcept
            : ara::core::Exception(err)
        {
        }
    };

    /// @brief Error domain for the communication layer
    /// @details Follows the AUTOSAR ara::core::ErrorDomain pattern.
    ///          All communication errors are scoped within this domain.
    class ComErrorDomain : public ara::core::ErrorDomain
    {
    public:
        using Errc = ComErrc;
        using Exception = ComException;

        /// @brief Unique domain identifier: "COM\0" encoded as uint64
        static constexpr IdType kId = 0x00000000434F4D00ULL;

        /// @brief Default constructor
        constexpr ComErrorDomain() noexcept : ErrorDomain(kId) {}

        /// @brief Return the name of this error domain
        char const *Name() const noexcept override { return "Com"; }

        /// @brief Translate an error code value into a text message
        char const *Message(CodeType code) const noexcept override
        {
            switch (static_cast<Errc>(code))
            {
            case Errc::kSuccess:
                return "Success";
            case Errc::kServiceNotFound:
                return "Service not found";
            case Errc::kTimeout:
                return "Operation timed out";
            case Errc::kSerializationFailed:
                return "Serialization failed";
            case Errc::kInvalidHandle:
                return "Invalid handle";
            case Errc::kInvalidArgument:
                return "Invalid argument";
            case Errc::kNotInitialized:
                return "Not initialized";
            case Errc::kAlreadyInitialized:
                return "Already initialized";
            case Errc::kCommunicationError:
                return "Communication error";
            case Errc::kInternalError:
                return "Internal error";
            case Errc::kInvalidName:
                return "Invalid runtime name";
            case Errc::kNotFound:
                return "Not found";
            default:
                return "Unknown error";
            }
        }

        /// @brief Throw the exception type for the given ErrorCode
        void ThrowAsException(ara::core::ErrorCode const &errorCode) const noexcept(false) override
        {
            throw ComException(errorCode);
        }
    };

    namespace internal
    {
        constexpr ComErrorDomain kComErrorDomain;
    } // namespace internal

    /// @brief Get the global ComErrorDomain instance
    constexpr ara::core::ErrorDomain const &GetComErrorDomain() noexcept
    {
        return internal::kComErrorDomain;
    }

    /// @brief Create an ErrorCode within ComErrorDomain
    constexpr ara::core::ErrorCode MakeErrorCode(
        ComErrc code,
        ara::core::ErrorDomain::SupportDataType data = {}) noexcept
    {
        return ara::core::ErrorCode(
            static_cast<ara::core::ErrorDomain::CodeType>(code),
            GetComErrorDomain(),
            data);
    }

} // namespace com
