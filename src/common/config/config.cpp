#include "common/config/config.h"

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

namespace {

bool extract_string(const std::string& json, const std::string& key, std::string& out) {
    const std::string needle = "\"" + key + "\"";
    std::size_t pos = json.find(needle);
    if (pos == std::string::npos) {
        return false;
    }
    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) {
        return false;
    }
    pos = json.find('"', pos + 1);
    if (pos == std::string::npos) {
        return false;
    }
    const std::size_t end = json.find('"', pos + 1);
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
    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) {
        return false;
    }
    const std::string tail = json.substr(pos + 1);
    const std::size_t start = tail.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        return false;
    }
    out = std::strtol(tail.c_str() + start, nullptr, 10);
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
    long announcement_interval_ms = 0;
    long initial_inactivity_ms = 0;
    long general_inactivity_ms = 0;
    long alive_check_timeout_ms = 0;

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
