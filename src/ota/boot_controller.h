#pragma once

#include <string>

namespace ota {

class BootController {
public:
    virtual ~BootController() = default;

    virtual bool set_next_slot(const std::string& slot) = 0;
    virtual bool reboot() = 0;
    virtual bool mark_boot_success() = 0;
    virtual bool mark_boot_failed(const std::string& reason) = 0;
};

class MockBootController final : public BootController {
public:
    MockBootController(std::string state_dir);

    bool set_next_slot(const std::string& slot) override;
    bool reboot() override;
    bool mark_boot_success() override;
    bool mark_boot_failed(const std::string& reason) override;

    std::string next_slot() const;
    std::string last_error() const;
    bool reboot_attempted() const;

private:
    bool write_text(const std::string& path, const std::string& value) const;
    bool read_text(const std::string& path, std::string& value) const;

    std::string state_dir_;
};

}  // namespace ota

