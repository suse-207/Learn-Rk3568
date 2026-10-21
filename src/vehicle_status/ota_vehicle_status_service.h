#pragma once

#include <mutex>
#include <string>

#include "ota/ota_types.h"
#include "vehicle_status/vehicle_status_service.h"

namespace ota {
class OtaManager;
class SlotManager;
class VersionManager;
}

namespace vehicle_status {

class OtaVehicleStatusService final : public VehicleStatusService {
public:
    OtaVehicleStatusService(ota::OtaManager& manager,
                            ota::SlotManager& slots,
                            ota::VersionManager& versions,
                            std::string image_path);

    Errc GetStatus(std::string& version,
                   std::string& current_slot,
                   OtaState& ota_state) override;

    Errc StartUpdate(const std::string& version,
                     std::uint64_t package_size,
                     const std::string& sha256,
                     std::string& update_id) override;

    Errc GetProgress(const std::string& update_id,
                     OtaState& ota_state,
                     std::uint32_t& percent,
                     std::string& detail) override;

    Errc Rollback() override;

private:
    static OtaState MapState(ota::State state);

    ota::OtaManager& manager_;
    ota::SlotManager& slots_;
    ota::VersionManager& versions_;
    std::string image_path_;
    std::string active_update_id_;
    std::uint64_t update_counter_{0};

    mutable std::mutex mutex_;
};

}  // namespace vehicle_status
