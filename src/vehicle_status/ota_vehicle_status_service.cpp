#include "vehicle_status/ota_vehicle_status_service.h"

#include <fstream>

#include "ota/ota_manager.h"
#include "ota/ota_types.h"
#include "ota/slot_manager.h"
#include "ota/version_manager.h"

namespace vehicle_status {

OtaVehicleStatusService::OtaVehicleStatusService(
    ota::OtaManager& manager,
    ota::SlotManager& slots,
    ota::VersionManager& versions,
    std::string image_path)
    : manager_(manager),
      slots_(slots),
      versions_(versions),
      image_path_(std::move(image_path)) {}

OtaState OtaVehicleStatusService::MapState(ota::State state) {
    switch (state) {
        case ota::State::Idle:            return OtaState::kIdle;
        case ota::State::Checking:        return OtaState::kVerifying;
        case ota::State::Downloading:     return OtaState::kDownloading;
        case ota::State::Verifying:       return OtaState::kVerifying;
        case ota::State::Installing:      return OtaState::kInstalling;
        case ota::State::ReadyToSwitch:   return OtaState::kReadyToSwitch;
        case ota::State::Rebooting:       return OtaState::kRebooting;
        case ota::State::BootVerify:      return OtaState::kRebooting;
        case ota::State::Success:         return OtaState::kSuccess;
        case ota::State::Failed:          return OtaState::kFailed;
        case ota::State::Rollback:        return OtaState::kRollback;
    }
    return OtaState::kIdle;
}

Errc OtaVehicleStatusService::GetStatus(std::string& version,
                                        std::string& current_slot,
                                        OtaState& ota_state) {
    std::lock_guard<std::mutex> lock(mutex_);
    version = versions_.current_version();
    current_slot = slots_.current_slot();
    ota_state = MapState(manager_.progress().state);
    return Errc::kSuccess;
}

Errc OtaVehicleStatusService::StartUpdate(const std::string& version,
                                          std::uint64_t package_size,
                                          const std::string& sha256,
                                          std::string& update_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    std::ifstream file(image_path_, std::ios::binary | std::ios::ate);
    if (!file) {
        return Errc::kNotFound;
    }
    const std::uint64_t actual_size =
        static_cast<std::uint64_t>(file.tellg());
    if (actual_size != package_size) {
        return Errc::kInvalidArgument;
    }
    file.close();

    if (!manager_.start_update(image_path_, sha256, version)) {
        return Errc::kBusy;
    }

    active_update_id_ = "update-" + std::to_string(++update_counter_);
    update_id = active_update_id_;
    return Errc::kSuccess;
}

Errc OtaVehicleStatusService::GetProgress(const std::string& update_id,
                                          OtaState& ota_state,
                                          std::uint32_t& percent,
                                          std::string& detail) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (active_update_id_.empty() || active_update_id_ != update_id) {
        return Errc::kNotFound;
    }

    const ota::Progress progress = manager_.progress();
    ota_state = MapState(progress.state);
    percent = static_cast<std::uint32_t>(progress.percent);
    detail = progress.detail;
    return Errc::kSuccess;
}

Errc OtaVehicleStatusService::Rollback() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!manager_.rollback()) {
        return Errc::kRollbackFailed;
    }
    return Errc::kSuccess;
}

}  // namespace vehicle_status
