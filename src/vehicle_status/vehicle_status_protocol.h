#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace vehicle_status {

constexpr std::uint16_t kServiceId = 0x6100;
constexpr std::uint16_t kInstanceId = 0x0001;

constexpr std::uint16_t kMethodGetStatus = 0x0001;
constexpr std::uint16_t kMethodStartUpdate = 0x0002;
constexpr std::uint16_t kMethodGetProgress = 0x0003;
constexpr std::uint16_t kMethodRollback = 0x0004;

enum class Errc : std::uint32_t {
    kSuccess = 0x00,
    kInvalidArgument = 0x01,
    kNotFound = 0x02,
    kBusy = 0x03,
    kInternalError = 0x04,
    kNotReady = 0x05,
    kVerifyFailed = 0x06,
    kInstallFailed = 0x07,
    kRollbackFailed = 0x08,
};

enum class OtaState : std::uint8_t {
    kIdle = 0x00,
    kDownloading = 0x01,
    kVerifying = 0x02,
    kInstalling = 0x03,
    kReadyToSwitch = 0x04,
    kRebooting = 0x05,
    kSuccess = 0x06,
    kFailed = 0x07,
    kRollback = 0x08,
};

void AppendU16(std::vector<std::uint8_t>& out, std::uint16_t v);
void AppendU32(std::vector<std::uint8_t>& out, std::uint32_t v);
void AppendU64(std::vector<std::uint8_t>& out, std::uint64_t v);
void AppendString(std::vector<std::uint8_t>& out, const std::string& s);

bool ReadU16(const std::vector<std::uint8_t>& in, std::size_t& pos, std::uint16_t& v);
bool ReadU32(const std::vector<std::uint8_t>& in, std::size_t& pos, std::uint32_t& v);
bool ReadU64(const std::vector<std::uint8_t>& in, std::size_t& pos, std::uint64_t& v);
bool ReadString(const std::vector<std::uint8_t>& in, std::size_t& pos, std::string& s);

}  // namespace vehicle_status
