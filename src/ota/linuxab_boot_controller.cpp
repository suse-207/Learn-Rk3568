#include "ota/linuxab_boot_controller.h"

#include <cstdlib>

#include <sys/reboot.h>
#include <unistd.h>

#include "common/log/log.h"

namespace ota {

bool LinuxAbBootController::set_next_slot(const std::string& /*slot*/) {
    // Slot activation is handled by LinuxAbSlotManager::activate_inactive().
    return true;
}

bool LinuxAbBootController::reboot() {
    ::sync();
    ::reboot(RB_AUTOBOOT);
    return false;  // Should never return on success.
}

bool LinuxAbBootController::mark_boot_success() {
    const int ret = std::system("rkboot_control now");
    return ret == 0;
}

bool LinuxAbBootController::mark_boot_failed(const std::string& reason) {
    LOG_ERROR("boot failed: %s", reason.c_str());
    return true;
}

}  // namespace ota
