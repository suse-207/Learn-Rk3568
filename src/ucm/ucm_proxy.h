#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ota/ucm_types.h"

namespace com {
namespace proxy {
class BindHandle;
}  // namespace proxy
}  // namespace com

namespace ucm {

/// Client-side proxy for the UCM PackageManagement service: serializes each
/// operation and invokes it over SOME-IP via a com BindHandle. Every method
/// returns an ota::UcmErrc error code.
class UcmProxy {
public:
    explicit UcmProxy(com::proxy::BindHandle& handle) : handle_(handle) {}

    ota::UcmErrc TransferStart(std::uint64_t size,
                               std::string& out_transfer_id,
                               std::uint32_t& out_block_size);
    ota::UcmErrc TransferData(const std::string& transfer_id,
                              const std::vector<std::uint8_t>& block,
                              std::uint64_t block_counter);
    ota::UcmErrc TransferExit(const std::string& transfer_id);
    ota::UcmErrc DeleteTransfer(const std::string& transfer_id);

    ota::UcmErrc ProcessSwPackage(const std::string& transfer_id);
    ota::UcmErrc Activate();
    ota::UcmErrc Finish();
    ota::UcmErrc Cancel(const std::string& transfer_id);
    ota::UcmErrc Rollback();
    ota::UcmErrc RevertProcessedSwPackages();

    ota::UcmErrc GetId(std::string& id);
    ota::UcmErrc GetSwPackages(std::vector<ota::SwPackageInfo>& packages);
    ota::UcmErrc GetSwClusterInfo(std::vector<ota::SwClusterInfo>& clusters);
    ota::UcmErrc GetSwClusterChangeInfo(std::vector<ota::SwClusterInfo>& changes);
    ota::UcmErrc GetSwClusterDescription(const std::string& name, std::string& description);
    ota::UcmErrc GetSwProcessProgress(const std::string& transfer_id, ota::ProgressInfo& progress);
    ota::UcmErrc GetHistory(std::vector<ota::HistoryEntry>& history);

private:
    com::proxy::BindHandle& handle_;
};

}  // namespace ucm
