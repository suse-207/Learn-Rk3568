// Copyright 2024
// Licensed under the Apache License, Version 2.0

/// @file       com_error_domain.h
/// @brief      Communication error domain and error codes
/// @details    标准码按 AUTOSAR AP SWS_CM_10432 逐值对齐（1..19 + kUnknownError）；
///             项目自有码统一放在 0x1000 起的独立区间，永不与标准值冲突。
///             见 docs/adr/0001-public-api-ownership.md（C4）。

#pragma once

#include "ara/core/error_code.h"
#include "ara/core/error_domain.h"
#include "ara/core/exception.h"

namespace com
{

    /// @brief Communication error codes
    /// @details 前 20 项为 SWS_CM_10432 标准码（值不得改动）；
    ///          其后为项目扩展码，禁止复用标准值。
    enum class ComErrc : ara::core::ErrorDomain::CodeType
    {
        // ---- 标准码（SWS_CM_10432）----
        kUnknownError = -1,                  ///< Unknown/Other Error
        kServiceNotAvailable = 1,            ///< Service is not available
        kMaxSamplesReached = 2,              ///< Application holds more samples than committed in Subscribe()
        kNetworkBindingFailure = 3,          ///< Local failure detected by the network binding
        kGrantEnforcementError = 4,          ///< Request refused by Grant enforcement layer
        kPeerIsUnreachable = 5,              ///< TLS handshake fail
        kFieldValueIsNotValid = 6,           ///< Field value is not valid
        kSetHandlerNotSet = 7,               ///< SetHandler has not been registered
        kUnsetFailure = 8,                   ///< Failure detected by unset operation
        kSampleAllocationFailure = 9,        ///< Not enough memory for a sample
        kIllegalUseOfAllocate = 10,          ///< Allocation done via custom allocator
        kServiceNotOffered = 11,             ///< Service not offered
        kCommunicationLinkError = 12,        ///< Communication link is broken
        kNoClients = 13,                     ///< No clients connected
        kCommunicationStackError = 14,       ///< Communication stack error
        kInstanceIDCouldNotBeResolved = 15,  ///< InstanceID could not be resolved
        kMaxSampleCountNotRealizable = 16,   ///< Provided maxSampleCount not realizable
        kWrongMethodCallProcessingMode = 17, ///< Wrong processing mode
        kErroneousFileHandle = 18,           ///< Erroneous handle from FindService
        kCouldNotExecute = 19,               ///< Command could not be executed

        // ---- 项目扩展码（非标准），0x1000 起 ----
        kTimeout = 0x1000,             ///< 同步调用的调用方超时（超时归调用方，ADR C7）
        kSerializationFailed = 0x1001, ///< 序列化/反序列化失败
        kInvalidHandle = 0x1002,       ///< 绑定向导无效
        kInvalidArgument = 0x1003,     ///< 参数非法
        kNotInitialized = 0x1004,      ///< 未初始化
        kAlreadyInitialized = 0x1005,  ///< 重复初始化
        kInternalError = 0x1006,       ///< 内部错误
        kInvalidName = 0x1007,         ///< 绑定运行时名无效
        kNotFound = 0x1008,            ///< 实体不存在
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
    /// @details 域标识按标准：SWS_CM_11329 kId 0x8000000000001267，名称 "ComError"。
    class ComErrorDomain : public ara::core::ErrorDomain
    {
    public:
        using Errc = ComErrc;
        using Exception = ComException;

        /// @brief Unique domain identifier (SWS_CM_11329)
        static constexpr IdType kId = 0x8000000000001267ULL;

        /// @brief Default constructor
        constexpr ComErrorDomain() noexcept : ErrorDomain(kId) {}

        /// @brief Return the name of this error domain
        char const *Name() const noexcept override { return "ComError"; }

        /// @brief Translate an error code value into a text message
        char const *Message(CodeType code) const noexcept override
        {
            switch (static_cast<Errc>(code))
            {
            case Errc::kUnknownError:
                return "Unknown/Other Error";
            case Errc::kServiceNotAvailable:
                return "Service is not available";
            case Errc::kMaxSamplesReached:
                return "Application holds more samples than committed in Subscribe()";
            case Errc::kNetworkBindingFailure:
                return "Local failure detected by the network binding";
            case Errc::kGrantEnforcementError:
                return "Request refused by Grant enforcement layer";
            case Errc::kPeerIsUnreachable:
                return "TLS handshake fail";
            case Errc::kFieldValueIsNotValid:
                return "Field value is not valid";
            case Errc::kSetHandlerNotSet:
                return "SetHandler has not been registered";
            case Errc::kUnsetFailure:
                return "Failure detected by unset operation";
            case Errc::kSampleAllocationFailure:
                return "Not enough memory for a sample";
            case Errc::kIllegalUseOfAllocate:
                return "Allocation done via custom allocator";
            case Errc::kServiceNotOffered:
                return "Service not offered";
            case Errc::kCommunicationLinkError:
                return "Communication link is broken";
            case Errc::kNoClients:
                return "No clients connected";
            case Errc::kCommunicationStackError:
                return "Communication stack error";
            case Errc::kInstanceIDCouldNotBeResolved:
                return "InstanceID could not be resolved";
            case Errc::kMaxSampleCountNotRealizable:
                return "Provided maxSampleCount not realizable";
            case Errc::kWrongMethodCallProcessingMode:
                return "Wrong processing mode";
            case Errc::kErroneousFileHandle:
                return "Erroneous handle from FindService";
            case Errc::kCouldNotExecute:
                return "Command could not be executed";
            case Errc::kTimeout:
                return "Operation timed out (caller-owned timeout)";
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
