#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "ota/ota_manager.h"
#include "ota/package_manifest.h"
#include "ota/ucm_types.h"
#include "ota/verify_strategy.h"
#include "ota/version_manager.h"

namespace ota {

/// Target-side UCM service, modeled after the ara::ucm PackageManagement
/// service interface (17 methods). Every method returns a UcmErrc error code.
class UcmService {
public:
    UcmService(std::string transfer_dir,
               OtaManager& manager,
               VersionManager& versions,
               VerifyStrategy& verify,
               std::string ucm_id);

    // -- transfer --
    UcmErrc TransferStart(std::uint64_t size,
                          std::string& out_transfer_id,
                          std::uint32_t& out_block_size);
    UcmErrc TransferData(const std::string& transfer_id,
                         const std::vector<std::uint8_t>& block,
                         std::uint64_t block_counter);
    UcmErrc TransferExit(const std::string& transfer_id);
    UcmErrc DeleteTransfer(const std::string& transfer_id);

    // -- process / lifecycle --
    UcmErrc ProcessSwPackage(const std::string& transfer_id);
    UcmErrc Activate();
    UcmErrc Finish();
    UcmErrc Rollback();
    UcmErrc Cancel(const std::string& transfer_id);
    UcmErrc RevertProcessedSwPackages();

    // -- query --
    UcmErrc GetId(std::string& id) const;
    UcmErrc GetSwPackages(std::vector<SwPackageInfo>& packages) const;
    UcmErrc GetSwClusterInfo(std::vector<SwClusterInfo>& clusters) const;
    UcmErrc GetSwClusterChangeInfo(std::vector<SwClusterInfo>& changes) const;
    UcmErrc GetSwClusterDescription(const std::string& name, std::string& description) const;
    UcmErrc GetSwProcessProgress(const std::string& transfer_id, ProgressInfo& progress) const;
    UcmErrc GetHistory(std::vector<HistoryEntry>& history) const;

    std::string PackagePath(const std::string& transfer_id) const;

private:
    struct Session {
        std::uint64_t expected_bytes{0};
        std::uint64_t received_bytes{0};
        std::uint64_t expected_block{1};
    };

    std::string MetaPath(const std::string& transfer_id) const;
    void SaveSession(const std::string& transfer_id, const Session& session);
    bool LoadSession(const std::string& transfer_id, Session& session);
    void RemoveSessionFiles(const std::string& transfer_id);

    std::string transfer_dir_;
    OtaManager& manager_;
    VersionManager& versions_;
    VerifyStrategy& verify_;
    std::string ucm_id_;
    std::map<std::string, Session> sessions_;
    std::uint64_t next_id_{1};
};

}  // namespace ota
