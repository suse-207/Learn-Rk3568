#pragma once

#include <string>

#include "ota/boot_controller.h"

namespace ota {

/// Real boot controller for the RK3568 Linux A/B system. Reboot is performed
/// through the kernel reboot(2) syscall; boot-success marking is delegated to
/// the board's rkboot_control tool.
class LinuxAbBootController final : public BootController {
public:
    LinuxAbBootController() = default;

    bool set_next_slot(const std::string& slot) override;
    bool reboot() override;
    bool mark_boot_success() override;
    bool mark_boot_failed(const std::string& reason) override;
};

}  // namespace ota
