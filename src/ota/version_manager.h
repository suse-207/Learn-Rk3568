#pragma once

#include <string>
#include <vector>

namespace ota {

struct VersionRecord {
    std::string from;
    std::string to;
    std::string result;
};

class VersionManager {
public:
    explicit VersionManager(std::string base_dir);

    std::string current_version() const;
    std::string target_version() const;
    const std::vector<VersionRecord>& history() const;

    void set_current(const std::string& version);
    void set_target(const std::string& version);
    void record(const std::string& from, const std::string& to, const std::string& result);

private:
    void load();
    void save();

    std::string base_dir_;
    std::string current_;
    std::string target_;
    std::vector<VersionRecord> history_;
};

}  // namespace ota

