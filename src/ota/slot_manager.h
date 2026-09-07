#pragma once

#include <string>

namespace ota {

class SlotManager {
public:
    virtual ~SlotManager() = default;

    virtual std::string current_slot() const = 0;
    virtual std::string inactive_slot() const = 0;
    virtual bool write_image_to_inactive(const std::string& image_path, const std::string& version) = 0;
    virtual bool activate_inactive() = 0;
    virtual bool rollback_to_last_good() = 0;
    virtual bool is_inactive_valid() const = 0;
};

class MockSlotManager final : public SlotManager {
public:
    MockSlotManager(std::string base_dir, std::string current = "A");

    std::string current_slot() const override;
    std::string inactive_slot() const override;
    bool write_image_to_inactive(const std::string& image_path, const std::string& version) override;
    bool activate_inactive() override;
    bool rollback_to_last_good() override;
    bool is_inactive_valid() const override;

private:
    bool ensure_dirs() const;
    bool write_text(const std::string& path, const std::string& value) const;
    bool read_text(const std::string& path, std::string& value) const;
    std::string slot_dir(const std::string& slot) const;

    std::string base_dir_;
    std::string current_;
    std::string last_good_;
};

}  // namespace ota

