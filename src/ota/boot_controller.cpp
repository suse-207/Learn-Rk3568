#include "ota/boot_controller.h"

#include <fstream>
#include <sstream>

#include <sys/stat.h>

namespace ota {

MockBootController::MockBootController(std::string state_dir) : state_dir_(std::move(state_dir)) {
    ::mkdir(state_dir_.c_str(), 0755);
}

bool MockBootController::set_next_slot(const std::string& slot) {
    return write_text(state_dir_ + "/next_slot", slot);
}

bool MockBootController::reboot() {
    return write_text(state_dir_ + "/boot_attempted", "1");
}

bool MockBootController::mark_boot_success() {
    return write_text(state_dir_ + "/boot_result", "success");
}

bool MockBootController::mark_boot_failed(const std::string& reason) {
    return write_text(state_dir_ + "/boot_result", "failed") &&
           write_text(state_dir_ + "/boot_error", reason);
}

std::string MockBootController::next_slot() const {
    std::string value;
    read_text(state_dir_ + "/next_slot", value);
    return value;
}

std::string MockBootController::last_error() const {
    std::string value;
    read_text(state_dir_ + "/boot_error", value);
    return value;
}

bool MockBootController::reboot_attempted() const {
    std::string value;
    read_text(state_dir_ + "/boot_attempted", value);
    return value == "1";
}

bool MockBootController::write_text(const std::string& path, const std::string& value) const {
    std::ofstream output(path, std::ios::trunc);
    if (!output) {
        return false;
    }
    output << value;
    return static_cast<bool>(output);
}

bool MockBootController::read_text(const std::string& path, std::string& value) const {
    std::ifstream input(path);
    if (!input) {
        return false;
    }
    std::stringstream ss;
    ss << input.rdbuf();
    value = ss.str();
    return true;
}

}  // namespace ota

