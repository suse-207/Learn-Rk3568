#include "vehicle_status/vehicle_status_proxy.h"

#include <chrono>

#include "com/proxy.h"

namespace vehicle_status {

VehicleStatusProxy::VehicleStatusProxy(com::proxy::BindHandle& handle)
    : handle_(handle) {}

Errc VehicleStatusProxy::GetStatus(std::string& version,
                                   std::string& current_slot,
                                   OtaState& ota_state) {
    std::vector<std::uint8_t> request;
    auto result = handle_.SendRequest(
        kMethodGetStatus, request, std::chrono::seconds(5));
    if (!result) {
        return Errc::kInternalError;
    }

    const std::vector<std::uint8_t>& data = result.Value();
    std::size_t pos = 0;
    std::uint32_t errc = 0;
    if (!ReadU32(data, pos, errc)) {
        return Errc::kInternalError;
    }
    if (errc != 0) {
        return static_cast<Errc>(errc);
    }

    if (!ReadString(data, pos, version) ||
        !ReadString(data, pos, current_slot) ||
        pos >= data.size()) {
        return Errc::kInternalError;
    }
    ota_state = static_cast<OtaState>(data[pos]);
    return Errc::kSuccess;
}

Errc VehicleStatusProxy::StartUpdate(const std::string& version,
                                     std::uint64_t package_size,
                                     const std::string& sha256,
                                     std::string& update_id) {
    std::vector<std::uint8_t> request;
    AppendString(request, version);
    AppendU64(request, package_size);
    AppendString(request, sha256);

    auto result = handle_.SendRequest(
        kMethodStartUpdate, request, std::chrono::seconds(5));
    if (!result) {
        return Errc::kInternalError;
    }

    const std::vector<std::uint8_t>& data = result.Value();
    std::size_t pos = 0;
    std::uint32_t errc = 0;
    if (!ReadU32(data, pos, errc)) {
        return Errc::kInternalError;
    }
    if (errc != 0) {
        return static_cast<Errc>(errc);
    }
    if (!ReadString(data, pos, update_id)) {
        return Errc::kInternalError;
    }
    return Errc::kSuccess;
}

Errc VehicleStatusProxy::GetProgress(const std::string& update_id,
                                     OtaState& ota_state,
                                     std::uint32_t& percent,
                                     std::string& detail) {
    std::vector<std::uint8_t> request;
    AppendString(request, update_id);

    auto result = handle_.SendRequest(
        kMethodGetProgress, request, std::chrono::seconds(5));
    if (!result) {
        return Errc::kInternalError;
    }

    const std::vector<std::uint8_t>& data = result.Value();
    std::size_t pos = 0;
    std::uint32_t errc = 0;
    if (!ReadU32(data, pos, errc)) {
        return Errc::kInternalError;
    }
    if (errc != 0) {
        return static_cast<Errc>(errc);
    }

    if (pos >= data.size()) {
        return Errc::kInternalError;
    }
    ota_state = static_cast<OtaState>(data[pos++]);
    if (!ReadU32(data, pos, percent) ||
        !ReadString(data, pos, detail)) {
        return Errc::kInternalError;
    }
    return Errc::kSuccess;
}

Errc VehicleStatusProxy::Rollback() {
    std::vector<std::uint8_t> request;
    auto result = handle_.SendRequest(
        kMethodRollback, request, std::chrono::seconds(5));
    if (!result) {
        return Errc::kInternalError;
    }

    const std::vector<std::uint8_t>& data = result.Value();
    std::size_t pos = 0;
    std::uint32_t errc = 0;
    if (!ReadU32(data, pos, errc)) {
        return Errc::kInternalError;
    }
    return static_cast<Errc>(errc);
}

}  // namespace vehicle_status
