#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "vehicle_status/vehicle_status_protocol.h"
#include "vehicle_status/vehicle_status_service.h"

namespace com {
namespace skeleton {
class BindSkeleton;
}
}

namespace vehicle_status {

class VehicleStatusServiceSkeleton {
public:
    explicit VehicleStatusServiceSkeleton(VehicleStatusService& service);

    void Register(com::skeleton::BindSkeleton& bind);
    void RegisterEvent(com::skeleton::BindSkeleton& bind);
    void NotifyState(OtaState state, std::uint32_t percent, const std::string& detail);

private:
    VehicleStatusService& service_;
    std::vector<com::skeleton::BindSkeleton*> binds_;
};

}  // namespace vehicle_status
