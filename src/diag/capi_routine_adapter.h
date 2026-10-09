#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "isoft/uds/result.h"
#include "isoft/uds/routine_management/generic_routine.h"
#include "isoft/uds/uds_nrc_error_domain.h"

namespace ota {
class OtaManager;
}

namespace diag {

class CapiRoutineAdapter final : public isoft::uds::server::GenericRoutineInterface {
public:
    CapiRoutineAdapter(ota::OtaManager* ota_manager, std::string download_dir);

    isoft::uds::Result<OperationOutput> Start(
        std::uint16_t routineId,
        std::vector<std::uint8_t> requestData,
        isoft::uds::server::MetaInfoMap& metaInfo,
        isoft::uds::server::CancellationHandler cancellationHandler) override;

    isoft::uds::Result<OperationOutput> Stop(
        std::uint16_t routineId,
        std::vector<std::uint8_t> requestData,
        isoft::uds::server::MetaInfoMap& metaInfo,
        isoft::uds::server::CancellationHandler cancellationHandler) override;

    isoft::uds::Result<OperationOutput> RequestResults(
        std::uint16_t routineId,
        std::vector<std::uint8_t> requestData,
        isoft::uds::server::MetaInfoMap& metaInfo,
        isoft::uds::server::CancellationHandler cancellationHandler) override;

private:
    static isoft::uds::Result<OperationOutput> Error(isoft::uds::server::NrcErrc nrc);

    ota::OtaManager* ota_manager_;
    std::string download_dir_;
};

}  // namespace diag
