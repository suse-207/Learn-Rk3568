#include "doip/doip_discovery.h"

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

constexpr std::size_t kMaxUdpPacket = 4096;

void write_be16(std::uint8_t* p, std::uint16_t v) {
    p[0] = static_cast<std::uint8_t>(v >> 8);
    p[1] = static_cast<std::uint8_t>(v & 0xFF);
}

}  // namespace

bool DoipDiscovery::set_nonblocking(int fd) {
    const int flags = ::fcntl(fd, F_GETFL, 0);
    return flags >= 0 && ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

bool DoipDiscovery::make_socket(const std::string& bind_ip, std::uint16_t port) {
    udp_fd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_fd_ < 0) {
        return false;
    }

    const int reuse = 1;
    ::setsockopt(udp_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    const int broadcast = 1;
    ::setsockopt(udp_fd_, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast));

    struct sockaddr_in addr {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (bind_ip.empty() || bind_ip == "0.0.0.0") {
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
    } else {
        ::inet_pton(AF_INET, bind_ip.c_str(), &addr.sin_addr);
    }

    if (::bind(udp_fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) != 0) {
        ::close(udp_fd_);
        udp_fd_ = -1;
        return false;
    }
    set_nonblocking(udp_fd_);
    return true;
}

bool DoipDiscovery::make_timer() {
    timer_fd_ = ::timerfd_create(CLOCK_MONOTONIC, 0);
    if (timer_fd_ < 0) {
        return false;
    }
    set_nonblocking(timer_fd_);

    const std::int64_t ms = identity_.announcement_interval.count();
    if (ms <= 0) {
        ::close(timer_fd_);
        timer_fd_ = -1;
        return false;
    }

    struct itimerspec its {};
    its.it_interval.tv_sec = ms / 1000;
    its.it_interval.tv_nsec = (ms % 1000) * 1000000L;
    its.it_value.tv_sec = its.it_interval.tv_sec;
    its.it_value.tv_nsec = its.it_interval.tv_nsec;
    return ::timerfd_settime(timer_fd_, 0, &its, nullptr) == 0;
}

bool DoipDiscovery::start(const std::string& bind_ip, std::uint16_t port,
                          EpollLoop& loop, const VehicleIdentity& identity,
                          PacketHandler handler) {
    identity_ = identity;
    handler_ = std::move(handler);
    loop_ = &loop;
    port_ = port;

    if (!make_socket(bind_ip, port)) {
        LOG_ERROR("DoIP discovery UDP socket bind failed on %s:%u", bind_ip.c_str(), port);
        return false;
    }
    if (!make_timer()) {
        LOG_ERROR("DoIP announcement timer create failed");
        return false;
    }

    loop.add(udp_fd_, EPOLLIN, [this](std::uint32_t events) { on_udp_read(events); });
    loop.add(timer_fd_, EPOLLIN,
             [this](std::uint32_t events) { on_announcement_timer(events); });

    send_announcement();
    return true;
}

void DoipDiscovery::stop() {
    if (loop_ != nullptr) {
        if (udp_fd_ >= 0) {
            loop_->del(udp_fd_);
        }
        if (timer_fd_ >= 0) {
            loop_->del(timer_fd_);
        }
    }
    if (udp_fd_ >= 0) {
        ::close(udp_fd_);
        udp_fd_ = -1;
    }
    if (timer_fd_ >= 0) {
        ::close(timer_fd_);
        timer_fd_ = -1;
    }
}

void DoipDiscovery::on_udp_read(std::uint32_t) {
    for (;;) {
        std::array<std::uint8_t, kMaxUdpPacket> buf{};
        struct sockaddr_in src {};
        socklen_t src_len = sizeof(src);
        const ssize_t n = ::recvfrom(udp_fd_, buf.data(), buf.size(), 0,
                                     reinterpret_cast<struct sockaddr*>(&src), &src_len);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return;
            }
            if (errno == EINTR) {
                continue;
            }
            return;
        }
        if (handler_) {
            handler_(buf.data(), static_cast<std::size_t>(n), src);
        }
    }
}

void DoipDiscovery::on_announcement_timer(std::uint32_t) {
    std::uint64_t expirations = 0;
    const ssize_t n = ::read(timer_fd_, &expirations, sizeof(expirations));
    if (n <= 0) {
        return;
    }
    send_announcement();
}

void DoipDiscovery::send_announcement() {
    std::array<std::uint8_t, kVehicleAnnouncementLength> payload{};
    std::memcpy(&payload[0], identity_.vin.data(), identity_.vin.size());
    write_be16(&payload[17], identity_.logical_address);
    std::memcpy(&payload[19], identity_.eid.data(), identity_.eid.size());
    std::memcpy(&payload[25], identity_.gid.data(), identity_.gid.size());
    payload[31] = identity_.further_action;
    payload[32] = 0x00;  // VIN/GID synchronized

    struct sockaddr_in dst {};
    dst.sin_family = AF_INET;
    dst.sin_port = htons(port_);
    if (::inet_pton(AF_INET, identity_.announcement_addr.c_str(), &dst.sin_addr) != 1) {
        LOG_WARN("invalid DoIP announcement address: %s", identity_.announcement_addr.c_str());
        return;
    }
    send_frame(static_cast<std::uint16_t>(PayloadType::kVehicleAnnouncement),
               payload.data(), payload.size(), dst);
}

void DoipDiscovery::send_frame(std::uint16_t payload_type,
                               const std::uint8_t* payload, std::size_t len,
                               const struct sockaddr_in& dst) {
    std::array<std::uint8_t, kHeaderSize + kMaxUdpPacket> buf{};
    const std::size_t total = build_frame(buf.data(), buf.size(), payload_type, payload, len);
    if (total == 0) {
        return;
    }

    const ssize_t n = ::sendto(udp_fd_, buf.data(), total, 0,
                               reinterpret_cast<const struct sockaddr*>(&dst), sizeof(dst));
    if (n < 0) {
        LOG_WARN("udp sendto payload_type=0x%04x failed: %s", payload_type, std::strerror(errno));
        return;
    }
    LOG_INFO("udp send payload_type=0x%04x len=%zu", payload_type, len);
}

}  // namespace doip
