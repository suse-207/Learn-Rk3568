#pragma once

#include <netinet/in.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

#include "doip/doip_types.h"
#include "net/epoll/epoll_loop.h"

namespace doip {

class DoipDiscovery {
public:
    using PacketHandler = std::function<void(const std::uint8_t* data, std::size_t len,
                                             const struct sockaddr_in& src)>;

    bool start(const std::string& bind_ip, std::uint16_t port, EpollLoop& loop,
               const VehicleIdentity& identity, PacketHandler handler);
    void stop();

    void send_frame(std::uint16_t payload_type, const std::uint8_t* payload,
                    std::size_t len, const struct sockaddr_in& dst);
    void send_announcement();

private:
    bool make_socket(const std::string& bind_ip, std::uint16_t port);
    bool make_timer();
    bool set_nonblocking(int fd);
    void on_udp_read(std::uint32_t events);
    void on_announcement_timer(std::uint32_t events);

    VehicleIdentity identity_;
    PacketHandler handler_;
    EpollLoop* loop_ = nullptr;
    int udp_fd_ = -1;
    int timer_fd_ = -1;
    std::uint16_t port_ = 13400;
};

}  // namespace doip
