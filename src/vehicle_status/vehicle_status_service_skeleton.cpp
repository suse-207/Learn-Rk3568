#include "vehicle_status/vehicle_status_service_skeleton.h"

#include <iostream>
#include <set>

#include "com/skeleton.h"
#include "vehicle_status/vehicle_status_protocol.h"

namespace vehicle_status {
namespace {

// Render a payload as lowercase hex so the demo log shows what actually went
// over the wire instead of only the length. vsomeip 3.3.0 does not log message
// payloads itself, so this is the only place the bytes become visible on the
// board side.
void DumpBytes(const char* tag, const std::vector<std::uint8_t>& data) {
    static const char kHex[] = "0123456789abcdef";
    std::cout << tag << " len=" << data.size() << "B |";
    for (std::size_t i = 0; i < data.size(); ++i) {
        const std::uint8_t byte = data[i];
        std::cout << ' ' << kHex[byte >> 4] << kHex[byte & 0x0Fu];
    }
    std::cout << std::endl;
}

}  // namespace

VehicleStatusServiceSkeleton::VehicleStatusServiceSkeleton(
    VehicleStatusService& service)
    : service_(service) {}

void VehicleStatusServiceSkeleton::Register(
    com::skeleton::BindSkeleton& bind) {
    binds_.push_back(&bind);

    bind.RegisterMethodHandler(
        kMethodGetStatus,
        [this](const std::vector<std::uint8_t>& request,
               std::vector<std::uint8_t>& response) {
            std::cout << "[someip] <- REQUEST GetStatus(0x0001)" << std::endl;
            DumpBytes("[someip]    req", request);

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
            std::cout << "[someip] -> RESPONSE GetStatus errc="
                      << static_cast<std::uint32_t>(errc) << " slot="
                      << current_slot << std::endl;
            DumpBytes("[someip]    rsp", response);
        });

    bind.RegisterMethodHandler(
        kMethodStartUpdate,
        [this](const std::vector<std::uint8_t>& request,
               std::vector<std::uint8_t>& response) {
            std::size_t pos = 0;
            std::string version;
            std::uint64_t package_size = 0;
            std::string sha256;
            std::cout << "[someip] <- REQUEST StartUpdate(0x0002)" << std::endl;
            DumpBytes("[someip]    req", request);

            if (!ReadString(request, pos, version) ||
                !ReadU64(request, pos, package_size) ||
                !ReadString(request, pos, sha256)) {
                std::cout << "[someip] StartUpdate: bad payload" << std::endl;
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
            std::cout << "[someip] -> RESPONSE StartUpdate errc="
                      << static_cast<std::uint32_t>(errc) << " version="
                      << version << " size=" << package_size << std::endl;
            DumpBytes("[someip]    rsp", response);
        });

    bind.RegisterMethodHandler(
        kMethodGetProgress,
        [this](const std::vector<std::uint8_t>& request,
               std::vector<std::uint8_t>& response) {
            std::size_t pos = 0;
            std::string update_id;
            std::cout << "[someip] <- REQUEST GetProgress(0x0003)" << std::endl;
            DumpBytes("[someip]    req", request);
            if (!ReadString(request, pos, update_id)) {
                std::cout << "[someip] GetProgress: bad payload" << std::endl;
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
            std::cout << "[someip] -> RESPONSE GetProgress errc="
                      << static_cast<std::uint32_t>(errc) << " update_id="
                      << update_id << std::endl;
            DumpBytes("[someip]    rsp", response);
        });

    bind.RegisterMethodHandler(
        kMethodRollback,
        [this](const std::vector<std::uint8_t>& request,
               std::vector<std::uint8_t>& response) {
            std::cout << "[someip] <- REQUEST Rollback(0x0004)" << std::endl;
            DumpBytes("[someip]    req", request);
            const Errc errc = service_.Rollback();
            AppendU32(response, static_cast<std::uint32_t>(errc));
            std::cout << "[someip] -> RESPONSE Rollback errc="
                      << static_cast<std::uint32_t>(errc) << std::endl;
            DumpBytes("[someip]    rsp", response);
        });
}

void VehicleStatusServiceSkeleton::RegisterEvent(
    com::skeleton::BindSkeleton& bind) {
    std::set<com::EventGroupIdentifier> groups{kEventGroupId};
    bind.OfferEvent(kEventOtaStateChanged, groups, com::EventType::kEvent);
    std::cout << "[someip] event 0x" << std::hex << kEventOtaStateChanged
              << std::dec << " offered in eventgroup 0x" << std::hex
              << kEventGroupId << std::dec << std::endl;
}

void VehicleStatusServiceSkeleton::NotifyState(
    OtaState state, std::uint32_t percent, const std::string& detail) {
    std::vector<std::uint8_t> data;
    AppendOtaState(data, state, percent, detail);
    for (auto* bind : binds_) {
        bind->SendEvent(kEventOtaStateChanged, data);
    }
    std::cout << "[someip] -> NOTIFY OtaStateChanged state="
              << static_cast<std::uint32_t>(state) << " percent=" << percent
              << " detail=" << detail << std::endl;
}

}  // namespace vehicle_status
