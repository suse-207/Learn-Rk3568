#pragma once

#include <cstddef>
#include <cstdint>

namespace doip {

constexpr std::uint8_t kProtocolVersion = 0x02;  // ISO 13400-2:2012
constexpr std::size_t kHeaderSize = 8;
constexpr std::uint32_t kMaxPayloadLength = 0x00FFFFFF;  // DoIP 头长度字段为 24 位有效值

enum class PayloadType : std::uint16_t {
    kGenericNack                      = 0x0000,
    kVehicleIdentificationRequest     = 0x0001,
    kVehicleIdentificationRequestEid  = 0x0002,
    kVehicleIdentificationRequestVin  = 0x0003,
    kVehicleAnnouncement              = 0x0004,
    kRoutingActivationRequest         = 0x0005,
    kRoutingActivationResponse        = 0x0006,
    kAliveCheckRequest                = 0x0007,
    kAliveCheckResponse               = 0x0008,
    kEntityStatusRequest              = 0x4001,
    kEntityStatusResponse             = 0x4002,
    kPowerModeRequest                 = 0x4003,
    kPowerModeResponse                = 0x4004,
    kDiagMessage                      = 0x8001,
    kDiagMessagePositiveAck           = 0x8002,
    kDiagMessageNegativeAck           = 0x8003,
};

// Generic header NACK 的原因码（ISO 13400-2）
enum class GenericNackCode : std::uint8_t {
    kInvalidProtocolVersion = 0x00,
    kUnsupportedPayloadType = 0x01,
    kMessageTooLarge        = 0x02,
    kOutOfMemory            = 0x03,
    kInvalidPayloadLength   = 0x04,
};

// Routing Activation 响应码
enum class RoutingActivationCode : std::uint8_t {
    kUnknownSourceAddress        = 0x00,
    kAllSocketsActive            = 0x01,
    kDifferentSourceOnSocket     = 0x02,
    kSourceAlreadyActive         = 0x03,
    kMissingAuthentication       = 0x04,
    kRejectedConfirmation        = 0x05,
    kUnsupportedActivationType   = 0x06,
    kRequiresTls                 = 0x07,
    kSuccess                     = 0x10,
    kConfirmationRequired        = 0x11,
};

// Diagnostic message negative acknowledgement 原因码
enum class DiagNackCode : std::uint8_t {
    kInvalidSourceAddress = 0x02,
    kUnknownTargetAddress = 0x03,
    kMessageTooLarge      = 0x04,
    kOutOfMemory          = 0x05,
    kTargetUnreachable    = 0x06,
    kUnknownNetwork       = 0x07,
    kTransportProtocolError = 0x08,
};

struct Header {
    std::uint8_t version = 0;
    std::uint8_t inverse_version = 0;
    std::uint16_t payload_type = 0;
    std::uint32_t payload_length = 0;
};

// 从大端字节流解析 8 字节 DoIP 头；不足 8 字节返回 false
bool parse_header(const std::uint8_t* data, std::size_t len, Header& header);

// 组一个完整 DoIP 帧（8 字节头 + payload），返回总长度；缓冲不足返回 0
std::size_t build_frame(std::uint8_t* out, std::size_t capacity,
                        std::uint16_t payload_type,
                        const std::uint8_t* payload, std::size_t payload_len);

}  // namespace doip
