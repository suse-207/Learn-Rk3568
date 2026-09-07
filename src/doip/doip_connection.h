#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "doip/doip_frame.h"

namespace doip {

enum class DoipConnectionState : std::uint8_t {
    kInitialized = 0x01,
    kAuthentication = 0x02,
    kConfirmation = 0x03,
    kActive = 0x04,
    kFinalize = 0x05,
};

struct DoipFrame {
    std::uint16_t payload_type = 0;
    std::vector<std::uint8_t> payload;
};

class DoipConnection {
public:
    explicit DoipConnection(int fd);

    int fd() const { return fd_; }

    void append(const char* data, std::size_t len);
    bool next_frame(DoipFrame& frame);
    void consume(std::size_t consumed);
    bool peek_header(Header& header) const;
    std::size_t next_frame_total() const;

    bool routing_active() const { return state_ == DoipConnectionState::kActive; }
    DoipConnectionState state() const { return state_; }

    void set_state(DoipConnectionState state);
    void set_routing_active(std::uint16_t tester_address, std::uint8_t activation_type);

    std::uint16_t tester_address() const { return tester_address_; }
    std::uint8_t activation_type() const { return activation_type_; }

    void touch(std::chrono::milliseconds general_inactivity);
    bool initial_inactivity_expired() const;
    bool general_inactivity_expired() const;

    void arm_initial_inactivity(std::chrono::milliseconds duration);
    void arm_general_inactivity(std::chrono::milliseconds duration);
    void arm_alive_check(std::chrono::milliseconds duration);
    void clear_alive_check();

    bool alive_pending() const { return alive_pending_; }
    bool alive_timeout(std::chrono::milliseconds duration) const;

    void clear_buffer() { rx_.clear(); }
    std::size_t buffered_bytes() const { return rx_.size(); }

private:
    using Clock = std::chrono::steady_clock;

    int fd_ = -1;
    std::vector<std::uint8_t> rx_;
    DoipConnectionState state_ = DoipConnectionState::kInitialized;
    std::uint16_t tester_address_ = 0;
    std::uint8_t activation_type_ = 0;
    Clock::time_point initial_inactivity_deadline_{};
    Clock::time_point general_inactivity_deadline_{};
    Clock::time_point alive_sent_at_{};
    bool alive_pending_ = false;
};

}  // namespace doip
