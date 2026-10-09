#include "diag/capi_did_adapter.h"

namespace diag {

CapiDidAdapter::CapiDidAdapter() {
    constexpr std::uint16_t kVinDid = 0xF190U;
    const std::vector<std::uint8_t> vin{
        0x52, 0x4B, 0x33, 0x35, 0x36, 0x38, 0x50, 0x4C, 0x41, 0x54,
        0x46, 0x4F, 0x52, 0x4D, 0x30, 0x30, 0x31};
    values_[kVinDid] = vin;
}

isoft::uds::Result<std::list<isoft::uds::server::DiagnosticData>> CapiDidAdapter::Read(
    std::vector<std::uint16_t>& dataIdentifierTable,
    isoft::uds::server::MetaInfoMap const& /*metaInfo*/,
    isoft::uds::server::CancellationHandler /*cancellationHandler*/) noexcept {
    std::list<isoft::uds::server::DiagnosticData> out;
    for (const auto id : dataIdentifierTable) {
        const auto it = values_.find(id);
        if (it == values_.end()) {
            return isoft::uds::Result<std::list<isoft::uds::server::DiagnosticData>>::FromError(
                static_cast<std::int32_t>(isoft::uds::server::NrcErrc::kRequestOutOfRange));
        }
        out.push_back({id, it->second});
    }
    return isoft::uds::Result<std::list<isoft::uds::server::DiagnosticData>>::FromValue(std::move(out));
}

isoft::uds::Result<void> CapiDidAdapter::Write(
    isoft::uds::server::DiagnosticData requestDataRecord,
    isoft::uds::server::MetaInfoMap const& /*metaInfo*/,
    isoft::uds::server::CancellationHandler /*cancellationHandler*/) noexcept {
    values_[requestDataRecord.id] = std::move(requestDataRecord.record);
    return {};
}

}  // namespace diag
