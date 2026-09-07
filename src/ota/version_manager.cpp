#include "ota/version_manager.h"

#include <fstream>
#include <sstream>

#include <sys/stat.h>

namespace ota {

VersionManager::VersionManager(std::string base_dir) : base_dir_(std::move(base_dir)) {
    ::mkdir(base_dir_.c_str(), 0755);
    load();
}

std::string VersionManager::current_version() const {
    return current_;
}

std::string VersionManager::target_version() const {
    return target_;
}

const std::vector<VersionRecord>& VersionManager::history() const {
    return history_;
}

void VersionManager::set_current(const std::string& version) {
    current_ = version;
    save();
}

void VersionManager::set_target(const std::string& version) {
    target_ = version;
    save();
}

void VersionManager::record(const std::string& from, const std::string& to, const std::string& result) {
    history_.push_back({from, to, result});
    save();
}

void VersionManager::load() {
    std::ifstream current_file(base_dir_ + "/current_version");
    if (current_file) {
        std::getline(current_file, current_);
    }

    std::ifstream target_file(base_dir_ + "/target_version");
    if (target_file) {
        std::getline(target_file, target_);
    }

    std::ifstream history_file(base_dir_ + "/version_history.log");
    if (!history_file) {
        return;
    }
    std::string line;
    while (std::getline(history_file, line)) {
        const std::size_t first = line.find('|');
        const std::size_t second = line.find('|', first + 1);
        if (first == std::string::npos || second == std::string::npos) {
            continue;
        }
        history_.push_back({
            line.substr(0, first),
            line.substr(first + 1, second - first - 1),
            line.substr(second + 1),
        });
    }
}

void VersionManager::save() {
    std::ofstream current_file(base_dir_ + "/current_version", std::ios::trunc);
    current_file << current_;

    std::ofstream target_file(base_dir_ + "/target_version", std::ios::trunc);
    target_file << target_;

    std::ofstream history_file(base_dir_ + "/version_history.log", std::ios::trunc);
    for (const auto& record : history_) {
        history_file << record.from << '|' << record.to << '|' << record.result << '\n';
    }
}

}  // namespace ota

