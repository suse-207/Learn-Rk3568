#include "doip/doip_routing_table.h"

namespace doip {

bool DoipRoutingTable::is_active(std::uint16_t tester_address) const {
    return address_to_fd_.find(tester_address) != address_to_fd_.end();
}

int DoipRoutingTable::fd_for(std::uint16_t tester_address) const {
    const auto it = address_to_fd_.find(tester_address);
    return it == address_to_fd_.end() ? -1 : it->second;
}

void DoipRoutingTable::activate(int fd, std::uint16_t tester_address) {
    deactivate(fd);
    deactivate_address(tester_address);
    address_to_fd_[tester_address] = fd;
    fd_to_address_[fd] = tester_address;
}

void DoipRoutingTable::deactivate(int fd) {
    const auto it = fd_to_address_.find(fd);
    if (it != fd_to_address_.end()) {
        address_to_fd_.erase(it->second);
        fd_to_address_.erase(it);
    }
}

void DoipRoutingTable::deactivate_address(std::uint16_t tester_address) {
    const auto it = address_to_fd_.find(tester_address);
    if (it != address_to_fd_.end()) {
        fd_to_address_.erase(it->second);
        address_to_fd_.erase(it);
    }
}

bool DoipRoutingTable::at_capacity(std::uint8_t max_open_sockets) const {
    return active_count() >= max_open_sockets;
}

}  // namespace doip
