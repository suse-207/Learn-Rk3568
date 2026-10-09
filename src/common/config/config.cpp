#include "common/config/config.h"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

bool extract_string(const std::string& json, const std::string& key, std::string& out) {
    const std::string needle = "\"" + key + "\"";
    std::size_t pos = json.find(needle);
    if (pos == std::string::npos) {
        return false;
    }
    pos = json.find(":", pos + needle.size());
    if (pos == std::string::npos) {
        return false;
    }
    pos = json.find("\"", pos + 1);
    if (pos == std::string::npos) {
        return false;
    }
    const std::size_t end = json.find("\"", pos + 1);
    if (end == std::string::npos) {
        return false;
    }
    out = json.substr(pos + 1, end - pos - 1);
    return true;
}

bool extract_int(const std::string& json, const std::string& key, long& out) {
    const std::string needle = "\"" + key + "\"";
    std::size_t pos = json.find(needle);
    if (pos == std::string::npos) {
        return false;
    }
    pos = json.find(":", pos + needle.size());
    if (pos == std::string::npos) {
        return false;
    }
    const std::string tail = json.substr(pos + 1);
    const std::size_t start = tail.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        return false;
    }
    char* end = nullptr;
    out = std::strtol(tail.c_str() + start, &end, 0);
    return end != tail.c_str() + start;
}

bool extract_uint16_array(const std::string& json,
                          const std::string& key,
                          std::vector<std::uint16_t>& out) {
    const std::string needle = "\"" + key + "\"";
    std::size_t pos = json.find(needle);
    if (pos == std::string::npos) {
        return false;
    }
    pos = json.find("[", pos + needle.size());
    const std::size_t end = json.find("]", pos);
    if (pos == std::string::npos || end == std::string::npos) {
        return false;
    }

    std::vector<std::uint16_t> result;
    std::size_t cursor = pos + 1;
    while (cursor < end) {
        cursor = json.find_first_not_of(" \t\r\n,", cursor);
        if (cursor == std::string::npos || cursor >= end) {
            break;
        }
        char* parse_end = nullptr;
        const long value = std::strtol(json.c_str() + cursor, &parse_end, 0);
        if (parse_end == json.c_str() + cursor) {
            return false;
        }
        result.push_back(static_cast<std::uint16_t>(value));
        cursor = static_cast<std::size_t>(parse_end - json.c_str());
    }

    out = std::move(result);
    return true;
}

}  // namespace

bool load_config(const std::string& path, PlatformConfig& cfg) {
    std::ifstream file(path);
    if (!file) {
        return false;
    }
    std::stringstream ss;
    ss << file.rdbuf();
    const std::string json = ss.str();

    std::string ip;
    std::string download_dir;
    std::string announcement_addr;
    long port = 0;
    long backlog = 0;
    long max_events = 0;
    long logical_address = 0;
    long max_open_sockets = 0;
    long max_data_size = 0;
    long announcement_interval_ms = 0;
    long initial_inactivity_ms = 0;
    long general_inactivity_ms = 0;
    long alive_check_timeout_ms = 0;
    std::vector<std::uint16_t> functional_addresses;

    if (extract_string(json, "listen_ip", ip)) {
        cfg.listen_ip = ip;
    }
    if (extract_string(json, "download_dir", download_dir)) {
        cfg.download_dir = download_dir;
    }
    if (extract_string(json, "announcement_addr", announcement_addr)) {
        cfg.announcement_addr = announcement_addr;
    }
    if (extract_int(json, "listen_port", port)) {
        cfg.listen_port = static_cast<std::uint16_t>(port);
    }
    if (extract_int(json, "backlog", backlog)) {
        cfg.backlog = static_cast<int>(backlog);
    }
    if (extract_int(json, "max_events", max_events)) {
        cfg.max_events = static_cast<int>(max_events);
    }
    if (extract_int(json, "logical_address", logical_address)) {
        cfg.logical_address = static_cast<std::uint16_t>(logical_address);
    }
    if (extract_int(json, "max_open_sockets", max_open_sockets)) {
        cfg.max_open_sockets = static_cast<std::uint8_t>(max_open_sockets);
    }
    if (extract_int(json, "max_data_size", max_data_size)) {
        cfg.max_data_size = static_cast<std::uint32_t>(max_data_size);
    }
    if (extract_uint16_array(json, "functional_addresses", functional_addresses)) {
        cfg.functional_addresses = std::move(functional_addresses);
    }
    if (extract_int(json, "announcement_interval_ms", announcement_interval_ms)) {
        cfg.announcement_interval_ms = static_cast<int>(announcement_interval_ms);
    }
    if (extract_int(json, "initial_inactivity_ms", initial_inactivity_ms)) {
        cfg.initial_inactivity_ms = static_cast<int>(initial_inactivity_ms);
    }
    if (extract_int(json, "general_inactivity_ms", general_inactivity_ms)) {
        cfg.general_inactivity_ms = static_cast<int>(general_inactivity_ms);
    }
    if (extract_int(json, "alive_check_timeout_ms", alive_check_timeout_ms)) {
        cfg.alive_check_timeout_ms = static_cast<int>(alive_check_timeout_ms);
    }
    return true;
}
