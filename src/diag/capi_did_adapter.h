#pragma once

#include <cstdint>
#include <list>
#include <unordered_map>
#include <vector>

#include "isoft/uds/data_management/generic_data_identifier.h"
#include "isoft/uds/result.h"
#include "isoft/uds/uds_nrc_error_domain.h"

namespace diag {

class CapiDidAdapter final : public isoft::uds::server::GenericDataIdentifierInterface {
public:
    CapiDidAdapter();

    isoft::uds::Result<std::list<isoft::uds::server::DiagnosticData>> Read(
        std::vector<std::uint16_t>& dataIdentifierTable,
        isoft::uds::server::MetaInfoMap const& metaInfo,
        isoft::uds::server::CancellationHandler cancellationHandler) noexcept override;

    isoft::uds::Result<void> Write(
        isoft::uds::server::DiagnosticData requestDataRecord,
        isoft::uds::server::MetaInfoMap const& metaInfo,
        isoft::uds::server::CancellationHandler cancellationHandler) noexcept override;

private:
    std::unordered_map<std::uint16_t, std::vector<std::uint8_t>> values_;
};

}  // namespace diag
