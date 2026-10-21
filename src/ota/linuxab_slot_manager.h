#pragma once

#include <string>

#include "ota/slot_manager.h"

namespace ota {

/// Real A/B slot manager for the RK3568 Linux A/B system.
///
/// It reads the current slot from /proc/cmdline, writes the Rockchip RKIMAGE
/// OTA package (boot + rootfs items) into the inactive slot's block devices,
/// and activates/rolls back the slot through the board's rkboot_control tool.
class LinuxAbSlotManager final : public SlotManager {
public:
    LinuxAbSlotManager();

    std::string current_slot() const override;
    std::string inactive_slot() const override;
    bool write_image_to_inactive(const std::string& image_path,
                                 const std::string& version) override;
    bool activate_inactive() override;
    bool rollback_to_last_good() override;
    bool is_inactive_valid() const override;

private:
    bool detect_current_slot(std::string& slot) const;
    bool write_item(const std::string& image_path,
                    const std::string& item_name,
                    std::uint32_t item_offset,
                    std::uint32_t item_size,
                    const std::string& device,
                    std::string& error) const;

    std::string current_;
};

}  // namespace ota
