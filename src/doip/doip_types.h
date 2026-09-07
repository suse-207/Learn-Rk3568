#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <string>

namespace doip {

constexpr std::size_t kVinLength = 17;
constexpr std::size_t kEidLength = 6;
constexpr std::size_t kGidLength = 6;

constexpr std::size_t kVehicleIdentificationResponseLength = 32;
constexpr std::size_t kVehicleAnnouncementLength = 33;

constexpr std::size_t kRoutingActivationRequestLengthShort = 7;
constexpr std::size_t kRoutingActivationRequestLengthLong = 11;
constexpr std::size_t kRoutingActivationResponseLength = 9;

constexpr std::size_t kAliveCheckResponseLength = 2;
constexpr std::size_t kEntityStatusResponseLength = 7;
constexpr std::size_t kPowerModeResponseLength = 1;

constexpr std::size_t kDiagMessageMinimumLength = 5;
constexpr std::size_t kDiagNackLength = 5;

struct VehicleIdentity {
    std::array<std::uint8_t, kVinLength> vin{};
    std::uint16_t logical_address = 0x0E80;
    std::array<std::uint8_t, kEidLength> eid{};
    std::array<std::uint8_t, kGidLength> gid{};
    std::uint8_t further_action = 0x00;

    // DoIP entity status / power mode 信息
    std::uint8_t node_type = 0x01;          // 0x00=gateway, 0x01=node
    std::uint8_t max_open_sockets = 1;
    std::uint32_t max_data_size = 4096;
    std::uint8_t power_mode = 0x01;         // 0x00=not ready, 0x01=ready, 0x02=not supported

    // UDP Discovery / Announcement
    std::string announcement_addr = "255.255.255.255";
    std::chrono::milliseconds announcement_interval{5000};

    // TCP connection timers
    std::chrono::milliseconds initial_inactivity_timeout{2000};
    std::chrono::milliseconds general_inactivity_timeout{5000};
    std::chrono::milliseconds alive_check_timeout{3000};
};

}  // namespace doip
