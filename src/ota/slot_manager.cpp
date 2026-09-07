#include "ota/slot_manager.h"

#include <fstream>
#include <sstream>

#include <cerrno>
#include <sys/stat.h>
#include <sys/types.h>

#include "common/log/log.h"

namespace ota {
namespace {

bool mkdir_p(const std::string& path) {
    const mode_t mode = 0755;
    std::string current;
    std::size_t pos = 0;
    while (pos < path.size()) {
        const std::size_t slash = path.find('/', pos);
        const std::string part = path.substr(0, slash == std::string::npos ? path.size() : slash);
        current = part.empty() ? "/" : part;
        if (::mkdir(current.c_str(), mode) != 0 && errno != EEXIST) {
            return false;
        }
        if (slash == std::string::npos) {
            break;
        }
        pos = slash + 1;
    }
    return true;
}

}  // namespace

MockSlotManager::MockSlotManager(std::string base_dir, std::string current)
    : base_dir_(std::move(base_dir)), current_(std::move(current)), last_good_(current_) {
    ensure_dirs();
    write_text(base_dir_ + "/current_slot", current_);
    write_text(base_dir_ + "/last_good_slot", last_good_);
}

std::string MockSlotManager::current_slot() const {
    return current_;
}

std::string MockSlotManager::inactive_slot() const {
    return current_ == "A" ? "B" : "A";
}

bool MockSlotManager::write_image_to_inactive(const std::string& image_path, const std::string& version) {
    const std::string target = inactive_slot();
    if (!ensure_dirs()) {
        return false;
    }

    std::ifstream input(image_path, std::ios::binary);
    if (!input) {
        LOG_ERROR("open source image failed: %s", image_path.c_str());
        return false;
    }

    const std::string output_path = slot_dir(target) + "/rootfs.img";
    std::ofstream output(output_path, std::ios::binary | std::ios::trunc);
    if (!output) {
        LOG_ERROR("open target image failed: %s", output_path.c_str());
        return false;
    }
    output << input.rdbuf();
    output.flush();
    if (!output) {
        return false;
    }

    if (!write_text(slot_dir(target) + "/version", version)) {
        return false;
    }
    return true;
}

bool MockSlotManager::activate_inactive() {
    if (!is_inactive_valid()) {
        return false;
    }
    last_good_ = current_;
    current_ = inactive_slot();
    return write_text(base_dir_ + "/last_good_slot", last_good_) &&
           write_text(base_dir_ + "/current_slot", current_);
}

bool MockSlotManager::rollback_to_last_good() {
    if (last_good_.empty()) {
        return false;
    }
    current_ = last_good_;
    return write_text(base_dir_ + "/current_slot", current_);
}

bool MockSlotManager::is_inactive_valid() const {
    std::ifstream image(slot_dir(inactive_slot()) + "/rootfs.img", std::ios::binary);
    std::string version;
    return static_cast<bool>(image) && read_text(slot_dir(inactive_slot()) + "/version", version);
}

bool MockSlotManager::ensure_dirs() const {
    return mkdir_p(base_dir_) &&
           mkdir_p(slot_dir("A")) &&
           mkdir_p(slot_dir("B"));
}

bool MockSlotManager::write_text(const std::string& path, const std::string& value) const {
    std::ofstream output(path, std::ios::trunc);
    if (!output) {
        return false;
    }
    output << value;
    return static_cast<bool>(output);
}

bool MockSlotManager::read_text(const std::string& path, std::string& value) const {
    std::ifstream input(path);
    if (!input) {
        return false;
    }
    std::stringstream ss;
    ss << input.rdbuf();
    value = ss.str();
    return true;
}

std::string MockSlotManager::slot_dir(const std::string& slot) const {
    return base_dir_ + "/slots/" + slot;
}

}  // namespace ota
