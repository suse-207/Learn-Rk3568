#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

#include "ota/boot_controller.h"
#include "ota/ota_types.h"
#include "ota/slot_manager.h"
#include "ota/verify_strategy.h"
#include "ota/version_manager.h"

namespace ota {

class OtaManager {
public:
    OtaManager(VerifyStrategy& verify,
               SlotManager& slots,
               BootController& boot,
               VersionManager& versions,
               std::string staging_dir);
    ~OtaManager();

    OtaManager(const OtaManager&) = delete;
    OtaManager& operator=(const OtaManager&) = delete;

    bool start_update(const std::string& image_path,
                      const std::string& expected_digest,
                      const std::string& target_version);
    bool switch_and_reboot();
    bool report_boot_result(bool success, const std::string& error_reason = "");
    bool rollback();

    Progress progress() const;

private:
    void run_update(std::string image_path, std::string expected_digest, std::string target_version);
    void set_progress(State state, int percent, std::string detail);
    void set_progress_locked(State state, int percent, std::string detail);
    bool copy_file(const std::string& from, const std::string& to, std::string& error) const;

    VerifyStrategy& verify_;
    SlotManager& slots_;
    BootController& boot_;
    VersionManager& versions_;
    std::string staging_dir_;

    mutable std::mutex progress_mutex_;
    Progress progress_;
    std::thread worker_;
};

}  // namespace ota
