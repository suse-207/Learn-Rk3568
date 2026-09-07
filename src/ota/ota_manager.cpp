#include "ota/ota_manager.h"

#include <fstream>

#include <sys/stat.h>

#include "common/log/log.h"

namespace ota {

OtaManager::OtaManager(VerifyStrategy& verify,
                       SlotManager& slots,
                       BootController& boot,
                       VersionManager& versions,
                       std::string staging_dir)
    : verify_(verify),
      slots_(slots),
      boot_(boot),
      versions_(versions),
      staging_dir_(std::move(staging_dir)) {
    ::mkdir(staging_dir_.c_str(), 0755);
}

OtaManager::~OtaManager() {
    if (worker_.joinable()) {
        worker_.join();
    }
}

bool OtaManager::start_update(const std::string& image_path,
                              const std::string& expected_digest,
                              const std::string& target_version) {
    {
        std::lock_guard<std::mutex> lock(progress_mutex_);
        if (progress_.state != State::Idle &&
            progress_.state != State::Failed &&
            progress_.state != State::Success &&
            progress_.state != State::Rollback) {
            return false;
        }
        if (worker_.joinable()) {
            worker_.join();
        }
        progress_ = Progress{};
    }

    worker_ = std::thread([this, image_path, expected_digest, target_version]() {
        run_update(image_path, expected_digest, target_version);
    });
    return true;
}

bool OtaManager::switch_and_reboot() {
    std::lock_guard<std::mutex> lock(progress_mutex_);
    if (progress_.state != State::ReadyToSwitch) {
        return false;
    }

    const std::string next = slots_.inactive_slot();
    if (!boot_.set_next_slot(next)) {
        set_progress_locked(State::Failed, progress_.percent, "set next slot failed");
        return false;
    }
    if (!slots_.activate_inactive()) {
        set_progress_locked(State::Failed, progress_.percent, "activate inactive slot failed");
        return false;
    }
    if (!boot_.reboot()) {
        set_progress_locked(State::Failed, progress_.percent, "reboot command failed");
        return false;
    }

    progress_.state = State::Rebooting;
    progress_.percent = 100;
    progress_.detail = "waiting for boot verification";
    return true;
}

bool OtaManager::report_boot_result(bool success, const std::string& error_reason) {
    std::lock_guard<std::mutex> lock(progress_mutex_);
    if (progress_.state != State::Rebooting) {
        return false;
    }

    const std::string from = versions_.current_version();
    const std::string to = versions_.target_version();
    if (success) {
        if (!boot_.mark_boot_success()) {
            set_progress_locked(State::Failed, 100, "record boot success failed");
            return false;
        }
        versions_.record(from, to, "SUCCESS");
        versions_.set_current(to);
        set_progress_locked(State::Success, 100, "update success");
        return true;
    }

    if (!boot_.mark_boot_failed(error_reason)) {
        set_progress_locked(State::Failed, 100, "record boot failure failed");
        return false;
    }
    if (!slots_.rollback_to_last_good()) {
        set_progress_locked(State::Failed, 100, "rollback slot failed");
        return false;
    }
    versions_.record(from, to, "ROLLBACK");
    set_progress_locked(State::Rollback, 100, "rollback to last good slot");
    return true;
}

bool OtaManager::rollback() {
    std::lock_guard<std::mutex> lock(progress_mutex_);
    if (progress_.state != State::ReadyToSwitch && progress_.state != State::Rebooting) {
        return false;
    }
    if (!slots_.rollback_to_last_good()) {
        set_progress_locked(State::Failed, progress_.percent, "rollback slot failed");
        return false;
    }
    set_progress_locked(State::Rollback, 100, "manual rollback");
    return true;
}

Progress OtaManager::progress() const {
    std::lock_guard<std::mutex> lock(progress_mutex_);
    return progress_;
}

void OtaManager::run_update(std::string image_path, std::string expected_digest,
                            std::string target_version) {
    set_progress(State::Checking, 1, "checking source image");
    std::ifstream source(image_path, std::ios::binary);
    if (!source) {
        set_progress(State::Failed, 0, "source image not found");
        return;
    }
    source.close();

    const std::string staged = staging_dir_ + "/pending.img";
    set_progress(State::Downloading, 15, "copying image to staging");
    std::string error;
    if (!copy_file(image_path, staged, error)) {
        set_progress(State::Failed, 15, error);
        return;
    }

    set_progress(State::Verifying, 55, "verifying image digest");
    if (!verify_.verify(staged, expected_digest, error)) {
        set_progress(State::Failed, 55, error);
        return;
    }

    set_progress(State::Installing, 75, "writing image to inactive slot");
    if (!slots_.write_image_to_inactive(staged, target_version)) {
        set_progress(State::Failed, 75, "write inactive slot failed");
        return;
    }

    versions_.set_target(target_version);
    set_progress(State::ReadyToSwitch, 90, "ready to switch");
}

void OtaManager::set_progress(State state, int percent, std::string detail) {
    std::lock_guard<std::mutex> lock(progress_mutex_);
    set_progress_locked(state, percent, std::move(detail));
}

void OtaManager::set_progress_locked(State state, int percent, std::string detail) {
    progress_.state = state;
    progress_.percent = percent;
    progress_.detail = std::move(detail);
    LOG_INFO("OTA state=%s percent=%d detail=%s",
             state_to_string(state), percent, progress_.detail.c_str());
}

bool OtaManager::copy_file(const std::string& from, const std::string& to, std::string& error) const {
    std::ifstream input(from, std::ios::binary);
    std::ofstream output(to, std::ios::binary | std::ios::trunc);
    if (!input || !output) {
        error = "copy file failed";
        return false;
    }
    output << input.rdbuf();
    output.flush();
    if (input.bad() || output.bad()) {
        error = "copy file failed during write";
        return false;
    }
    return true;
}

}  // namespace ota
