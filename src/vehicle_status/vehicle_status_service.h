#pragma once

#include <cstdint>
#include <string>

#include "vehicle_status/vehicle_status_protocol.h"

namespace vehicle_status {

class VehicleStatusService {
public:
    virtual ~VehicleStatusService() = default;

    virtual Errc GetStatus(std::string& version,
                           std::string& current_slot,
                           OtaState& ota_state) = 0;

    virtual Errc StartUpdate(const std::string& version,
                             std::uint64_t package_size,
                             const std::string& sha256,
                             std::string& update_id) = 0;

    virtual Errc GetProgress(const std::string& update_id,
                             OtaState& ota_state,
                             std::uint32_t& percent,
                             std::string& detail) = 0;

    virtual Errc Rollback() = 0;
};

}  // namespace vehicle_status
