#pragma once

#include <cstdint>
#include <string>

#include "vehicle_status/vehicle_status_protocol.h"

namespace com {
namespace proxy {
class BindHandle;
}
}

namespace vehicle_status {

class VehicleStatusProxy {
public:
    explicit VehicleStatusProxy(com::proxy::BindHandle& handle);

    Errc GetStatus(std::string& version,
                   std::string& current_slot,
                   OtaState& ota_state);

    Errc StartUpdate(const std::string& version,
                     std::uint64_t package_size,
                     const std::string& sha256,
                     std::string& update_id);

    Errc GetProgress(const std::string& update_id,
                     OtaState& ota_state,
                     std::uint32_t& percent,
                     std::string& detail);

    Errc Rollback();

private:
    com::proxy::BindHandle& handle_;
};

}  // namespace vehicle_status
