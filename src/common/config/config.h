#pragma once

#include <cstdint>
#include <string>

struct PlatformConfig {
    std::string listen_ip = "0.0.0.0";
    std::uint16_t listen_port = 9000;
    int backlog = 128;
    int max_events = 1024;
    std::string download_dir = "/tmp";
    std::string announcement_addr = "255.255.255.255";
    int announcement_interval_ms = 5000;
    int initial_inactivity_ms = 2000;
    int general_inactivity_ms = 5000;
    int alive_check_timeout_ms = 3000;
};

bool load_config(const std::string& path, PlatformConfig& cfg);
