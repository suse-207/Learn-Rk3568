#pragma once

#include <netinet/in.h>

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "doip/doip_connection.h"
#include "doip/doip_discovery.h"
#include "doip/doip_routing_table.h"
#include "doip/doip_types.h"
#include "isoft/uds/server.h"
#include "net/epoll/epoll_loop.h"

namespace doip {

class DoipChannel;

class DoipServer {
public:
    void set_identity(const VehicleIdentity& identity) { identity_ = identity; }
    void set_functional_addresses(std::unordered_set<std::uint16_t> addresses) {
        functional_addresses_ = std::move(addresses);
    }
    void set_uds_server(std::shared_ptr<isoft::uds::server::Server> server) {
        udsServer_ = std::move(server);
    }

    bool start_discovery(const std::string& bind_ip, std::uint16_t port, EpollLoop& loop);
    void stop_discovery();

    void on_data(int fd, const char* data, std::size_t len);
    void erase_client(int fd);

    bool SendDiagMessage(int fd,
                         std::uint16_t source_addr,
                         std::uint16_t target_addr,
                         const std::vector<std::uint8_t>& body);

private:
    void dispatch(int fd, std::uint16_t payload_type,
                  const std::uint8_t* payload, std::size_t len);
    void send_frame(int fd, std::uint16_t payload_type,
                    const std::uint8_t* payload, std::size_t len);

    void handle_vehicle_identification(int fd, std::uint16_t request_type,
                                       const std::uint8_t* payload, std::size_t len);
    void handle_routing_activation(int fd, const std::uint8_t* payload, std::size_t len);
    void handle_alive_check(int fd);
    void handle_diag_message(int fd, const std::uint8_t* payload, std::size_t len);
    void handle_entity_status_request(int fd);
    void handle_power_mode_request(int fd);

    void send_vehicle_identification_payload(int fd);
    void send_generic_nack(int fd, std::uint8_t code);
    void send_routing_activation_response(int fd, std::uint16_t tester_addr,
                                          std::uint8_t response_code);
    void send_diag_nack(int fd, std::uint8_t code, std::uint16_t source_addr,
                        std::uint16_t target_addr);

    void on_udp_packet(const std::uint8_t* data, std::size_t len,
                       const struct sockaddr_in& src);
    void handle_vehicle_identification_udp(std::uint16_t request_type,
                                           const std::uint8_t* payload, std::size_t len,
                                           const struct sockaddr_in& src);
    void handle_entity_status_udp(const struct sockaddr_in& src);
    void handle_power_mode_udp(const struct sockaddr_in& src);
    void send_generic_nack_udp(std::uint8_t code, const struct sockaddr_in& dst);

    void request_close(int fd);
    void maintain_connections();

    bool is_functional_address(std::uint16_t address) const {
        return functional_addresses_.find(address) != functional_addresses_.end();
    }

    VehicleIdentity identity_;
    std::unordered_set<std::uint16_t> functional_addresses_{0x7DFU};
    std::shared_ptr<isoft::uds::server::Server> udsServer_;
    DoipDiscovery discovery_;
    DoipRoutingTable routing_;
    std::unordered_map<int, DoipConnection> connections_;
    std::map<int, std::shared_ptr<DoipChannel>> channels_;

    EpollLoop* loop_ = nullptr;
    int maintenance_timer_fd_ = -1;
};

}  // namespace doip
