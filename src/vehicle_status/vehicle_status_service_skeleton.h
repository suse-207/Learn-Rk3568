#pragma once

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

private:
    VehicleStatusService& service_;
};

}  // namespace vehicle_status
