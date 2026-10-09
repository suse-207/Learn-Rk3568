#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct PlatformConfig {
    std::string listen_ip = "0.0.0.0";
    std::uint16_t listen_port = 13400;
    int backlog = 128;
    int max_events = 1024;

    std::uint16_t logical_address = 0x0E80;
    std::uint8_t max_open_sockets = 1;
    std::vector<std::uint16_t> functional_addresses = {0x7DFU};
    std::uint32_t max_data_size = 4096;

    std::string download_dir = "/tmp";
    std::string announcement_addr = "255.255.255.255";
    int announcement_interval_ms = 5000;
    int initial_inactivity_ms = 2000;
    int general_inactivity_ms = 5000;
    int alive_check_timeout_ms = 3000;
};

bool load_config(const std::string& path, PlatformConfig& cfg);
