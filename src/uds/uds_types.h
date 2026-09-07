#pragma once

#include <cstdint>
#include <vector>

namespace uds {

constexpr std::uint8_t kSidDiagSessionControl    = 0x10;
constexpr std::uint8_t kSidReadDataByIdentifier  = 0x22;
constexpr std::uint8_t kSidWriteDataByIdentifier = 0x2E;
constexpr std::uint8_t kSidRoutineControl        = 0x31;
constexpr std::uint8_t kSidRequestDownload       = 0x34;
constexpr std::uint8_t kSidTransferData          = 0x36;
constexpr std::uint8_t kSidRequestTransferExit   = 0x37;

constexpr std::uint8_t kNrcGeneralReject                = 0x10;
constexpr std::uint8_t kNrcServiceNotSupported          = 0x11;
constexpr std::uint8_t kNrcSubFunctionNotSupported      = 0x12;
constexpr std::uint8_t kNrcIncorrectMessageLength       = 0x13;
constexpr std::uint8_t kNrcConditionsNotCorrect         = 0x22;
constexpr std::uint8_t kNrcRequestOutOfRange            = 0x31;
constexpr std::uint8_t kNrcUploadDownloadNotAccepted    = 0x70;
constexpr std::uint8_t kNrcWrongBlockSequenceCounter    = 0x73;
constexpr std::uint8_t kNrcServiceNotSupportedInSession = 0x7F;

enum class Session : std::uint8_t {
    kDefault     = 0x01,
    kProgramming = 0x02,
    kExtended    = 0x03,
};

struct UdsRequest {
    std::uint8_t sid = 0;
    std::vector<std::uint8_t> params;  // SID 之后的全部字节
};

struct UdsResponse {
    bool negative = false;
    std::uint8_t nrc = 0;
    std::vector<std::uint8_t> data;  // positive 时是 SID+0x40 之后的数据
};

}  // namespace uds
