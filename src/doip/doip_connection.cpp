#include "doip/doip_connection.h"

#include <algorithm>

#include "doip/doip_frame.h"

namespace doip {

DoipConnection::DoipConnection(int fd) : fd_(fd) {}

void DoipConnection::append(const char* data, std::size_t len) {
    rx_.insert(rx_.end(), data, data + len);
}

bool DoipConnection::next_frame(DoipFrame& frame) {
    Header header;
    if (!peek_header(header) || header.payload_length > kMaxPayloadLength) {
        return false;
    }

    const std::size_t total = kHeaderSize + header.payload_length;
    if (rx_.size() < total) {
        return false;
    }

    frame.payload_type = header.payload_type;
    frame.payload.assign(rx_.begin() + kHeaderSize, rx_.begin() + total);
    return true;
}

bool DoipConnection::peek_header(Header& header) const {
    if (rx_.size() < kHeaderSize) {
        return false;
    }
    return parse_header(rx_.data(), rx_.size(), header);
}

std::size_t DoipConnection::next_frame_total() const {
    Header header;
    if (!peek_header(header) || header.payload_length > kMaxPayloadLength) {
        return 0;
    }
    return kHeaderSize + header.payload_length;
}

void DoipConnection::consume(std::size_t consumed) {
    if (consumed >= rx_.size()) {
        rx_.clear();
    } else {
        rx_.erase(rx_.begin(), rx_.begin() + consumed);
    }
}

void DoipConnection::set_state(DoipConnectionState state) {
    state_ = state;
}

void DoipConnection::set_routing_active(std::uint16_t tester_address,
                                        std::uint8_t activation_type) {
    state_ = DoipConnectionState::kActive;
    tester_address_ = tester_address;
    activation_type_ = activation_type;
}

void DoipConnection::touch(std::chrono::milliseconds general_inactivity) {
    if (state_ == DoipConnectionState::kActive) {
        arm_general_inactivity(general_inactivity);
    }
}

void DoipConnection::arm_initial_inactivity(std::chrono::milliseconds duration) {
    initial_inactivity_deadline_ = Clock::now() + duration;
}

void DoipConnection::arm_general_inactivity(std::chrono::milliseconds duration) {
    general_inactivity_deadline_ = Clock::now() + duration;
}

void DoipConnection::arm_alive_check(std::chrono::milliseconds duration) {
    alive_pending_ = true;
    alive_sent_at_ = Clock::now();
    general_inactivity_deadline_ = alive_sent_at_ + duration;
}

void DoipConnection::clear_alive_check() {
    alive_pending_ = false;
    alive_sent_at_ = {};
}

bool DoipConnection::initial_inactivity_expired() const {
    return Clock::now() >= initial_inactivity_deadline_;
}

bool DoipConnection::general_inactivity_expired() const {
    return Clock::now() >= general_inactivity_deadline_;
}

bool DoipConnection::alive_timeout(std::chrono::milliseconds duration) const {
    return alive_pending_ && (Clock::now() >= alive_sent_at_ + duration);
}

}  // namespace doip
