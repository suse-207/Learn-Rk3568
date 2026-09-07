#pragma once

#include <cstdint>
#include <unordered_map>

namespace doip {

class DoipRoutingTable {
public:
    bool is_active(std::uint16_t tester_address) const;
    int fd_for(std::uint16_t tester_address) const;

    void activate(int fd, std::uint16_t tester_address);
    void deactivate(int fd);
    void deactivate_address(std::uint16_t tester_address);

    std::size_t active_count() const { return address_to_fd_.size(); }
    bool at_capacity(std::uint8_t max_open_sockets) const;

private:
    std::unordered_map<std::uint16_t, int> address_to_fd_;
    std::unordered_map<int, std::uint16_t> fd_to_address_;
};

}  // namespace doip
