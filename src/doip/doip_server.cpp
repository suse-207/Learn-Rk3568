#include "doip/doip_server.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/timerfd.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstring>

#include "common/log/log.h"
#include "doip/doip_frame.h"

namespace doip {
namespace {

constexpr std::chrono::milliseconds kMaintenanceInterval{1000};

std::uint16_t read_be16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>((p[0] << 8) | p[1]);
}

void write_be16(std::uint8_t* p, std::uint16_t v) {
    p[0] = static_cast<std::uint8_t>(v >> 8);
    p[1] = static_cast<std::uint8_t>(v & 0xFF);
}

void write_be32(std::uint8_t* p, std::uint32_t v) {
    p[0] = static_cast<std::uint8_t>((v >> 24) & 0xFF);
    p[1] = static_cast<std::uint8_t>((v >> 16) & 0xFF);
    p[2] = static_cast<std::uint8_t>((v >> 8) & 0xFF);
    p[3] = static_cast<std::uint8_t>(v & 0xFF);
}

bool set_nonblocking(int fd) {
    const int flags = ::fcntl(fd, F_GETFL, 0);
    return flags >= 0 && ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

}  // namespace

bool DoipServer::start_discovery(const std::string& bind_ip, std::uint16_t port,
                                 EpollLoop& loop) {
    loop_ = &loop;

    maintenance_timer_fd_ = ::timerfd_create(CLOCK_MONOTONIC, 0);
    if (maintenance_timer_fd_ < 0) {
        return false;
    }
    set_nonblocking(maintenance_timer_fd_);

    struct itimerspec its {};
    its.it_interval.tv_sec = kMaintenanceInterval.count() / 1000;
    its.it_interval.tv_nsec = (kMaintenanceInterval.count() % 1000) * 1000000L;
    its.it_value = its.it_interval;
    if (::timerfd_settime(maintenance_timer_fd_, 0, &its, nullptr) != 0) {
        ::close(maintenance_timer_fd_);
        maintenance_timer_fd_ = -1;
        return false;
    }
    loop.add(maintenance_timer_fd_, EPOLLIN,
             [this](std::uint32_t) { maintain_connections(); });

    if (!discovery_.start(bind_ip, port, loop, identity_,
                          [this](const std::uint8_t* data, std::size_t len,
                                 const struct sockaddr_in& src) {
                              on_udp_packet(data, len, src);
                          })) {
        loop.del(maintenance_timer_fd_);
        ::close(maintenance_timer_fd_);
        maintenance_timer_fd_ = -1;
        return false;
    }
    return true;
}

void DoipServer::stop_discovery() {
    if (loop_ != nullptr && maintenance_timer_fd_ >= 0) {
        loop_->del(maintenance_timer_fd_);
    }
    if (maintenance_timer_fd_ >= 0) {
        ::close(maintenance_timer_fd_);
        maintenance_timer_fd_ = -1;
    }
    discovery_.stop();
}

void DoipServer::on_data(int fd, const char* data, std::size_t len) {
    auto [it, inserted] = connections_.try_emplace(fd, fd);
    DoipConnection& conn = it->second;
    if (inserted) {
        conn.arm_initial_inactivity(identity_.initial_inactivity_timeout);
    }
    conn.append(data, len);
    conn.touch(identity_.general_inactivity_timeout);

    for (;;) {
        Header header;
        if (!conn.peek_header(header)) {
            return;
        }
        if (header.version != kProtocolVersion ||
            header.inverse_version != static_cast<std::uint8_t>(~header.version)) {
            send_generic_nack(fd,
                              static_cast<std::uint8_t>(GenericNackCode::kInvalidProtocolVersion));
            request_close(fd);
            return;
        }
        if (header.payload_length > kMaxPayloadLength) {
            send_generic_nack(fd,
                              static_cast<std::uint8_t>(GenericNackCode::kMessageTooLarge));
            request_close(fd);
            return;
        }

        DoipFrame frame;
        if (!conn.next_frame(frame)) {
            return;
        }
        const std::size_t consumed = conn.next_frame_total();
        if (consumed == 0) {
            return;
        }
        conn.consume(consumed);
        dispatch(fd, frame.payload_type, frame.payload.data(), frame.payload.size());
    }
}

void DoipServer::dispatch(int fd, std::uint16_t payload_type,
                          const std::uint8_t* payload, std::size_t len) {
    LOG_INFO("fd=%d recv payload_type=0x%04x len=%zu", fd, payload_type, len);

    switch (payload_type) {
        case static_cast<std::uint16_t>(PayloadType::kVehicleIdentificationRequest):
        case static_cast<std::uint16_t>(PayloadType::kVehicleIdentificationRequestEid):
        case static_cast<std::uint16_t>(PayloadType::kVehicleIdentificationRequestVin):
            handle_vehicle_identification(fd, payload_type, payload, len);
            break;
        case static_cast<std::uint16_t>(PayloadType::kRoutingActivationRequest):
            handle_routing_activation(fd, payload, len);
            break;
        case static_cast<std::uint16_t>(PayloadType::kAliveCheckRequest):
            handle_alive_check(fd);
            break;
        case static_cast<std::uint16_t>(PayloadType::kEntityStatusRequest):
            handle_entity_status_request(fd);
            break;
        case static_cast<std::uint16_t>(PayloadType::kPowerModeRequest):
            handle_power_mode_request(fd);
            break;
        case static_cast<std::uint16_t>(PayloadType::kDiagMessage):
            handle_diag_message(fd, payload, len);
            break;
        default:
            send_generic_nack(fd,
                              static_cast<std::uint8_t>(GenericNackCode::kUnsupportedPayloadType));
            break;
    }
}

void DoipServer::handle_vehicle_identification(int fd, std::uint16_t request_type,
                                               const std::uint8_t* payload, std::size_t len) {
    if (request_type == static_cast<std::uint16_t>(PayloadType::kVehicleIdentificationRequestEid)) {
        if (len < identity_.eid.size() ||
            std::memcmp(payload, identity_.eid.data(), identity_.eid.size()) != 0) {
            return;
        }
    } else if (request_type ==
               static_cast<std::uint16_t>(PayloadType::kVehicleIdentificationRequestVin)) {
        if (len < identity_.vin.size() ||
            std::memcmp(payload, identity_.vin.data(), identity_.vin.size()) != 0) {
            return;
        }
    }
    send_vehicle_identification_payload(fd);
}

void DoipServer::send_vehicle_identification_payload(int fd) {
    std::array<std::uint8_t, kVehicleIdentificationResponseLength> payload{};
    std::memcpy(&payload[0], identity_.vin.data(), identity_.vin.size());
    write_be16(&payload[17], identity_.logical_address);
    std::memcpy(&payload[19], identity_.eid.data(), identity_.eid.size());
    std::memcpy(&payload[25], identity_.gid.data(), identity_.gid.size());
    payload[31] = identity_.further_action;
    send_frame(fd, static_cast<std::uint16_t>(PayloadType::kVehicleAnnouncement),
               payload.data(), payload.size());
}

void DoipServer::handle_routing_activation(int fd, const std::uint8_t* payload, std::size_t len) {
    if (len != kRoutingActivationRequestLengthShort &&
        len != kRoutingActivationRequestLengthLong) {
        send_generic_nack(fd,
                          static_cast<std::uint8_t>(GenericNackCode::kInvalidPayloadLength));
        return;
    }

    const std::uint16_t tester_addr = read_be16(payload);
    const std::uint8_t activation_type = payload[2];
    auto it = connections_.find(fd);
    if (it == connections_.end()) {
        return;
    }
    DoipConnection& conn = it->second;

    if (conn.routing_active() && conn.tester_address() != tester_addr) {
        send_routing_activation_response(
            fd, tester_addr,
            static_cast<std::uint8_t>(RoutingActivationCode::kDifferentSourceOnSocket));
        request_close(fd);
        return;
    }

    const int active_fd = routing_.fd_for(tester_addr);
    if (active_fd >= 0 && active_fd != fd) {
        send_routing_activation_response(
            fd, tester_addr,
            static_cast<std::uint8_t>(RoutingActivationCode::kSourceAlreadyActive));
        request_close(fd);
        return;
    }

    if (!conn.routing_active() && routing_.at_capacity(identity_.max_open_sockets)) {
        send_routing_activation_response(
            fd, tester_addr,
            static_cast<std::uint8_t>(RoutingActivationCode::kAllSocketsActive));
        request_close(fd);
        return;
    }

    if (activation_type != 0x00) {
        send_routing_activation_response(
            fd, tester_addr,
            static_cast<std::uint8_t>(RoutingActivationCode::kUnsupportedActivationType));
        request_close(fd);
        return;
    }

    routing_.activate(fd, tester_addr);
    conn.set_routing_active(tester_addr, activation_type);
    conn.arm_general_inactivity(identity_.general_inactivity_timeout);
    send_routing_activation_response(
        fd, tester_addr, static_cast<std::uint8_t>(RoutingActivationCode::kSuccess));
    LOG_INFO("fd=%d routing active tester=0x%04x type=0x%02x",
             fd, tester_addr, activation_type);
}

void DoipServer::send_routing_activation_response(int fd, std::uint16_t tester_addr,
                                                  std::uint8_t response_code) {
    std::array<std::uint8_t, kRoutingActivationResponseLength> resp{};
    write_be16(&resp[0], tester_addr);
    write_be16(&resp[2], identity_.logical_address);
    resp[4] = response_code;
    send_frame(fd, static_cast<std::uint16_t>(PayloadType::kRoutingActivationResponse),
               resp.data(), resp.size());
}

void DoipServer::handle_alive_check(int fd) {
    auto it = connections_.find(fd);
    if (it != connections_.end()) {
        it->second.clear_alive_check();
        it->second.arm_general_inactivity(identity_.general_inactivity_timeout);
    }
    std::array<std::uint8_t, kAliveCheckResponseLength> resp{};
    write_be16(resp.data(), identity_.logical_address);
    send_frame(fd, static_cast<std::uint16_t>(PayloadType::kAliveCheckResponse),
               resp.data(), resp.size());
}

void DoipServer::handle_diag_message(int fd, const std::uint8_t* payload, std::size_t len) {
    auto it = connections_.find(fd);
    if (it == connections_.end() || !it->second.routing_active()) {
        return;
    }

    if (len < kDiagMessageMinimumLength) {
        send_generic_nack(fd,
                          static_cast<std::uint8_t>(GenericNackCode::kInvalidPayloadLength));
        return;
    }

    const std::uint16_t source_addr = read_be16(payload);
    const std::uint16_t target_addr = read_be16(payload + 2);

    if (source_addr != it->second.tester_address()) {
        send_diag_nack(fd, static_cast<std::uint8_t>(DiagNackCode::kInvalidSourceAddress),
                       target_addr, source_addr);
        return;
    }
    if (target_addr != identity_.logical_address) {
        send_diag_nack(fd, static_cast<std::uint8_t>(DiagNackCode::kUnknownTargetAddress),
                       target_addr, source_addr);
        return;
    }
    if (len - 4 > identity_.max_data_size) {
        send_diag_nack(fd, static_cast<std::uint8_t>(DiagNackCode::kMessageTooLarge),
                       target_addr, source_addr);
        return;
    }

    const std::vector<std::uint8_t> resp = uds_.Handle(fd, payload + 4, len - 4);
    if (resp.empty()) {
        return;
    }

    std::vector<std::uint8_t> out;
    out.reserve(4 + resp.size());
    out.push_back(static_cast<std::uint8_t>(identity_.logical_address >> 8));
    out.push_back(static_cast<std::uint8_t>(identity_.logical_address & 0xFF));
    out.push_back(static_cast<std::uint8_t>(source_addr >> 8));
    out.push_back(static_cast<std::uint8_t>(source_addr & 0xFF));
    out.insert(out.end(), resp.begin(), resp.end());
    send_frame(fd, static_cast<std::uint16_t>(PayloadType::kDiagMessage),
               out.data(), out.size());
}

void DoipServer::send_diag_nack(int fd, std::uint8_t code, std::uint16_t source_addr,
                                std::uint16_t target_addr) {
    std::array<std::uint8_t, kDiagNackLength> payload{};
    write_be16(&payload[0], source_addr);
    write_be16(&payload[2], target_addr);
    payload[4] = code;
    send_frame(fd, static_cast<std::uint16_t>(PayloadType::kDiagMessageNegativeAck),
               payload.data(), payload.size());
}

void DoipServer::handle_entity_status_request(int fd) {
    std::array<std::uint8_t, kEntityStatusResponseLength> resp{};
    resp[0] = identity_.node_type;
    resp[1] = identity_.max_open_sockets;
    resp[2] = static_cast<std::uint8_t>(routing_.active_count());
    write_be32(&resp[3], identity_.max_data_size);
    send_frame(fd, static_cast<std::uint16_t>(PayloadType::kEntityStatusResponse),
               resp.data(), resp.size());
}

void DoipServer::handle_power_mode_request(int fd) {
    std::array<std::uint8_t, kPowerModeResponseLength> resp{identity_.power_mode};
    send_frame(fd, static_cast<std::uint16_t>(PayloadType::kPowerModeResponse),
               resp.data(), resp.size());
}

void DoipServer::send_generic_nack(int fd, std::uint8_t code) {
    std::array<std::uint8_t, 1> payload{code};
    send_frame(fd, static_cast<std::uint16_t>(PayloadType::kGenericNack), payload.data(), 1);
}

void DoipServer::send_frame(int fd, std::uint16_t payload_type,
                            const std::uint8_t* payload, std::size_t len) {
    std::array<std::uint8_t, kHeaderSize + 4096> buf;
    const std::size_t total = build_frame(buf.data(), buf.size(), payload_type, payload, len);
    if (total == 0) {
        return;
    }

    std::size_t sent = 0;
    while (sent < total) {
        const ssize_t w = ::send(fd, buf.data() + sent, total - sent, 0);
        if (w <= 0) {
            return;
        }
        sent += static_cast<std::size_t>(w);
    }
    LOG_INFO("fd=%d send payload_type=0x%04x len=%zu", fd, payload_type, len);
}

void DoipServer::on_udp_packet(const std::uint8_t* data, std::size_t len,
                               const struct sockaddr_in& src) {
    if (len < kHeaderSize) {
        return;
    }

    Header header;
    if (!parse_header(data, len, header) ||
        header.version != kProtocolVersion ||
        header.inverse_version != static_cast<std::uint8_t>(~header.version)) {
        send_generic_nack_udp(
            static_cast<std::uint8_t>(GenericNackCode::kInvalidProtocolVersion), src);
        return;
    }
    if (len < kHeaderSize + header.payload_length) {
        return;
    }

    const std::uint8_t* payload = data + kHeaderSize;
    switch (header.payload_type) {
        case static_cast<std::uint16_t>(PayloadType::kGenericNack):
        case static_cast<std::uint16_t>(PayloadType::kVehicleAnnouncement):
            break;
        case static_cast<std::uint16_t>(PayloadType::kVehicleIdentificationRequest):
        case static_cast<std::uint16_t>(PayloadType::kVehicleIdentificationRequestEid):
        case static_cast<std::uint16_t>(PayloadType::kVehicleIdentificationRequestVin):
            handle_vehicle_identification_udp(header.payload_type, payload,
                                              header.payload_length, src);
            break;
        case static_cast<std::uint16_t>(PayloadType::kEntityStatusRequest):
            handle_entity_status_udp(src);
            break;
        case static_cast<std::uint16_t>(PayloadType::kPowerModeRequest):
            handle_power_mode_udp(src);
            break;
        default:
            send_generic_nack_udp(
                static_cast<std::uint8_t>(GenericNackCode::kUnsupportedPayloadType), src);
            break;
    }
}

void DoipServer::handle_vehicle_identification_udp(std::uint16_t request_type,
                                                   const std::uint8_t* payload,
                                                   std::size_t len,
                                                   const struct sockaddr_in& src) {
    if (request_type == static_cast<std::uint16_t>(PayloadType::kVehicleIdentificationRequestEid)) {
        if (len < identity_.eid.size() ||
            std::memcmp(payload, identity_.eid.data(), identity_.eid.size()) != 0) {
            return;
        }
    } else if (request_type ==
               static_cast<std::uint16_t>(PayloadType::kVehicleIdentificationRequestVin)) {
        if (len < identity_.vin.size() ||
            std::memcmp(payload, identity_.vin.data(), identity_.vin.size()) != 0) {
            return;
        }
    }

    std::array<std::uint8_t, kVehicleIdentificationResponseLength> resp{};
    std::memcpy(&resp[0], identity_.vin.data(), identity_.vin.size());
    write_be16(&resp[17], identity_.logical_address);
    std::memcpy(&resp[19], identity_.eid.data(), identity_.eid.size());
    std::memcpy(&resp[25], identity_.gid.data(), identity_.gid.size());
    resp[31] = identity_.further_action;
    discovery_.send_frame(static_cast<std::uint16_t>(PayloadType::kVehicleAnnouncement),
                          resp.data(), resp.size(), src);
}

void DoipServer::handle_entity_status_udp(const struct sockaddr_in& src) {
    std::array<std::uint8_t, kEntityStatusResponseLength> resp{};
    resp[0] = identity_.node_type;
    resp[1] = identity_.max_open_sockets;
    resp[2] = static_cast<std::uint8_t>(routing_.active_count());
    write_be32(&resp[3], identity_.max_data_size);
    discovery_.send_frame(static_cast<std::uint16_t>(PayloadType::kEntityStatusResponse),
                          resp.data(), resp.size(), src);
}

void DoipServer::handle_power_mode_udp(const struct sockaddr_in& src) {
    std::array<std::uint8_t, kPowerModeResponseLength> resp{identity_.power_mode};
    discovery_.send_frame(static_cast<std::uint16_t>(PayloadType::kPowerModeResponse),
                          resp.data(), resp.size(), src);
}

void DoipServer::send_generic_nack_udp(std::uint8_t code, const struct sockaddr_in& dst) {
    std::array<std::uint8_t, 1> payload{code};
    discovery_.send_frame(static_cast<std::uint16_t>(PayloadType::kGenericNack),
                          payload.data(), payload.size(), dst);
}

void DoipServer::maintain_connections() {
    std::uint64_t expirations = 0;
    const ssize_t n = ::read(maintenance_timer_fd_, &expirations, sizeof(expirations));
    if (n <= 0) {
        return;
    }

    std::vector<int> to_close;
    for (auto& kv : connections_) {
        DoipConnection& conn = kv.second;
        if (!conn.routing_active() && conn.initial_inactivity_expired()) {
            to_close.push_back(kv.first);
            continue;
        }
        if (conn.routing_active() && conn.alive_timeout(identity_.alive_check_timeout)) {
            to_close.push_back(kv.first);
            continue;
        }
        if (conn.routing_active() &&
            conn.general_inactivity_expired() &&
            !conn.alive_pending()) {
            send_frame(kv.first, static_cast<std::uint16_t>(PayloadType::kAliveCheckRequest),
                       nullptr, 0);
            conn.arm_alive_check(identity_.alive_check_timeout);
        }
    }
    for (int fd : to_close) {
        request_close(fd);
    }
}

void DoipServer::request_close(int fd) {
    ::shutdown(fd, SHUT_RDWR);
}

void DoipServer::erase_client(int fd) {
    routing_.deactivate(fd);
    uds_.erase(fd);
    connections_.erase(fd);
}

}  // namespace doip
