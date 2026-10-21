#include "vehicle_status/vehicle_status_service_skeleton.h"

#include "com/skeleton.h"
#include "vehicle_status/vehicle_status_protocol.h"

namespace vehicle_status {

VehicleStatusServiceSkeleton::VehicleStatusServiceSkeleton(
    VehicleStatusService& service)
    : service_(service) {}

void VehicleStatusServiceSkeleton::Register(
    com::skeleton::BindSkeleton& bind) {
    bind.RegisterMethodHandler(
        kMethodGetStatus,
        [this](const std::vector<std::uint8_t>& request,
               std::vector<std::uint8_t>& response) {
            (void)request;

            std::string version;
            std::string current_slot;
            OtaState state = OtaState::kIdle;
            const Errc errc = service_.GetStatus(version, current_slot, state);

            AppendU32(response, static_cast<std::uint32_t>(errc));
            if (errc == Errc::kSuccess) {
                AppendString(response, version);
                AppendString(response, current_slot);
                response.push_back(static_cast<std::uint8_t>(state));
            }
        });

    bind.RegisterMethodHandler(
        kMethodStartUpdate,
        [this](const std::vector<std::uint8_t>& request,
               std::vector<std::uint8_t>& response) {
            std::size_t pos = 0;
            std::string version;
            std::uint64_t package_size = 0;
            std::string sha256;

            if (!ReadString(request, pos, version) ||
                !ReadU64(request, pos, package_size) ||
                !ReadString(request, pos, sha256)) {
                AppendU32(response,
                          static_cast<std::uint32_t>(Errc::kInvalidArgument));
                return;
            }

            std::string update_id;
            const Errc errc = service_.StartUpdate(
                version, package_size, sha256, update_id);

            AppendU32(response, static_cast<std::uint32_t>(errc));
            if (errc == Errc::kSuccess) {
                AppendString(response, update_id);
            }
        });

    bind.RegisterMethodHandler(
        kMethodGetProgress,
        [this](const std::vector<std::uint8_t>& request,
               std::vector<std::uint8_t>& response) {
            std::size_t pos = 0;
            std::string update_id;
            if (!ReadString(request, pos, update_id)) {
                AppendU32(response,
                          static_cast<std::uint32_t>(Errc::kInvalidArgument));
                return;
            }

            OtaState state = OtaState::kIdle;
            std::uint32_t percent = 0;
            std::string detail;
            const Errc errc = service_.GetProgress(
                update_id, state, percent, detail);

            AppendU32(response, static_cast<std::uint32_t>(errc));
            if (errc == Errc::kSuccess) {
                response.push_back(static_cast<std::uint8_t>(state));
                AppendU32(response, percent);
                AppendString(response, detail);
            }
        });

    bind.RegisterMethodHandler(
        kMethodRollback,
        [this](const std::vector<std::uint8_t>& request,
               std::vector<std::uint8_t>& response) {
            (void)request;
            const Errc errc = service_.Rollback();
            AppendU32(response, static_cast<std::uint32_t>(errc));
        });
}

}  // namespace vehicle_status
