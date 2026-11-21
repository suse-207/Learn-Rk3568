// Copyright 2024
// Licensed under the Apache License, Version 2.0

/// @file       main.cpp
/// @brief      错误码数值契约（ADR 0001 C4）
/// @details    标准码必须与 SWS_CM_10432 逐值一致；项目扩展码必须 >= 0x1000，
///             保证永不与标准值冲突。数值一旦被改动，这里编译期就会失败。

#include "com/api/com_error_domain.h"

#include <cstdint>

namespace
{
    template <typename T>
    constexpr std::int64_t Value(T v) noexcept
    {
        return static_cast<std::int64_t>(v);
    }
} // namespace

// ---- 标准码（SWS_CM_10432）----
static_assert(Value(com::ComErrc::kUnknownError) == -1, "SWS_CM_10432");
static_assert(Value(com::ComErrc::kServiceNotAvailable) == 1, "SWS_CM_10432");
static_assert(Value(com::ComErrc::kMaxSamplesReached) == 2, "SWS_CM_10432");
static_assert(Value(com::ComErrc::kNetworkBindingFailure) == 3, "SWS_CM_10432");
static_assert(Value(com::ComErrc::kGrantEnforcementError) == 4, "SWS_CM_10432");
static_assert(Value(com::ComErrc::kPeerIsUnreachable) == 5, "SWS_CM_10432");
static_assert(Value(com::ComErrc::kFieldValueIsNotValid) == 6, "SWS_CM_10432");
static_assert(Value(com::ComErrc::kSetHandlerNotSet) == 7, "SWS_CM_10432");
static_assert(Value(com::ComErrc::kUnsetFailure) == 8, "SWS_CM_10432");
static_assert(Value(com::ComErrc::kSampleAllocationFailure) == 9, "SWS_CM_10432");
static_assert(Value(com::ComErrc::kIllegalUseOfAllocate) == 10, "SWS_CM_10432");
static_assert(Value(com::ComErrc::kServiceNotOffered) == 11, "SWS_CM_10432");
static_assert(Value(com::ComErrc::kCommunicationLinkError) == 12, "SWS_CM_10432");
static_assert(Value(com::ComErrc::kNoClients) == 13, "SWS_CM_10432");
static_assert(Value(com::ComErrc::kCommunicationStackError) == 14, "SWS_CM_10432");
static_assert(Value(com::ComErrc::kInstanceIDCouldNotBeResolved) == 15, "SWS_CM_10432");
static_assert(Value(com::ComErrc::kMaxSampleCountNotRealizable) == 16, "SWS_CM_10432");
static_assert(Value(com::ComErrc::kWrongMethodCallProcessingMode) == 17, "SWS_CM_10432");
static_assert(Value(com::ComErrc::kErroneousFileHandle) == 18, "SWS_CM_10432");
static_assert(Value(com::ComErrc::kCouldNotExecute) == 19, "SWS_CM_10432");

// ---- 项目扩展码：必须 >= 0x1000，且互不相同 ----
static_assert(Value(com::ComErrc::kTimeout) >= 0x1000, "extension codes start at 0x1000");
static_assert(Value(com::ComErrc::kSerializationFailed) >= 0x1000, "extension");
static_assert(Value(com::ComErrc::kInvalidHandle) >= 0x1000, "extension");
static_assert(Value(com::ComErrc::kInvalidArgument) >= 0x1000, "extension");
static_assert(Value(com::ComErrc::kNotInitialized) >= 0x1000, "extension");
static_assert(Value(com::ComErrc::kAlreadyInitialized) >= 0x1000, "extension");
static_assert(Value(com::ComErrc::kInternalError) >= 0x1000, "extension");
static_assert(Value(com::ComErrc::kInvalidName) >= 0x1000, "extension");
static_assert(Value(com::ComErrc::kNotFound) >= 0x1000, "extension");

// ---- 域标识（SWS_CM_11329）----
static_assert(com::ComErrorDomain::kId == 0x8000000000001267ULL, "SWS_CM_11329");

int main()
{
    return 0;
}
