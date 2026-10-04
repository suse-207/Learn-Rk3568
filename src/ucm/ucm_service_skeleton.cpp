#include "ucm/ucm_service_skeleton.h"

#include <cstdint>
#include <string>
#include <vector>

#include "com/skeleton.h"
#include "ucm/ucm_protocol.h"

namespace ucm {

namespace {

inline void AppendErrc(std::vector<std::uint8_t>& resp, ota::UcmErrc e) {
    AppendU32(resp, static_cast<std::uint32_t>(e));
}

}  // namespace

void UcmServiceSkeleton::Register(com::skeleton::BindSkeleton& bind) {
    // TransferStart: req = u64(size); resp = errc + (ok: string(id) + u32(blockSize))
    bind.RegisterMethodHandler(kMethodTransferStart,
        [this](const std::vector<std::uint8_t>& req, std::vector<std::uint8_t>& resp) {
            std::size_t pos = 0;
            std::uint64_t size = 0;
            if (!ReadU64(req, pos, size)) {
                AppendErrc(resp, ota::UcmErrc::kOperationNotPermitted);
                return;
            }
            std::string id;
            std::uint32_t block_size = 0;
            const ota::UcmErrc errc = service_.TransferStart(size, id, block_size);
            AppendErrc(resp, errc);
            if (errc == ota::UcmErrc::kSuccess) {
                AppendString(resp, id);
                AppendU32(resp, block_size);
            }
        });

    // TransferData: req = string(id) + u64(counter) + bytes(block); resp = errc
    bind.RegisterMethodHandler(kMethodTransferData,
        [this](const std::vector<std::uint8_t>& req, std::vector<std::uint8_t>& resp) {
            std::size_t pos = 0;
            std::string id;
            std::uint64_t counter = 0;
            std::vector<std::uint8_t> block;
            if (!ReadString(req, pos, id) || !ReadU64(req, pos, counter) || !ReadBytes(req, pos, block)) {
                AppendErrc(resp, ota::UcmErrc::kOperationNotPermitted);
                return;
            }
            AppendErrc(resp, service_.TransferData(id, block, counter));
        });

    // TransferExit: req = string(id); resp = errc
    bind.RegisterMethodHandler(kMethodTransferExit,
        [this](const std::vector<std::uint8_t>& req, std::vector<std::uint8_t>& resp) {
            std::size_t pos = 0;
            std::string id;
            if (!ReadString(req, pos, id)) {
                AppendErrc(resp, ota::UcmErrc::kOperationNotPermitted);
                return;
            }
            AppendErrc(resp, service_.TransferExit(id));
        });

    // DeleteTransfer: req = string(id); resp = errc
    bind.RegisterMethodHandler(kMethodDeleteTransfer,
        [this](const std::vector<std::uint8_t>& req, std::vector<std::uint8_t>& resp) {
            std::size_t pos = 0;
            std::string id;
            if (!ReadString(req, pos, id)) {
                AppendErrc(resp, ota::UcmErrc::kOperationNotPermitted);
                return;
            }
            AppendErrc(resp, service_.DeleteTransfer(id));
        });

    // ProcessSwPackage: req = string(id); resp = errc
    bind.RegisterMethodHandler(kMethodProcessSwPackage,
        [this](const std::vector<std::uint8_t>& req, std::vector<std::uint8_t>& resp) {
            std::size_t pos = 0;
            std::string id;
            if (!ReadString(req, pos, id)) {
                AppendErrc(resp, ota::UcmErrc::kOperationNotPermitted);
                return;
            }
            AppendErrc(resp, service_.ProcessSwPackage(id));
        });

    // Activate: req = (none); resp = errc
    bind.RegisterMethodHandler(kMethodActivate,
        [this](const std::vector<std::uint8_t>&, std::vector<std::uint8_t>& resp) {
            AppendErrc(resp, service_.Activate());
        });

    // Finish: req = (none); resp = errc
    bind.RegisterMethodHandler(kMethodFinish,
        [this](const std::vector<std::uint8_t>&, std::vector<std::uint8_t>& resp) {
            AppendErrc(resp, service_.Finish());
        });

    // Cancel: req = string(id); resp = errc
    bind.RegisterMethodHandler(kMethodCancel,
        [this](const std::vector<std::uint8_t>& req, std::vector<std::uint8_t>& resp) {
            std::size_t pos = 0;
            std::string id;
            if (!ReadString(req, pos, id)) {
                AppendErrc(resp, ota::UcmErrc::kOperationNotPermitted);
                return;
            }
            AppendErrc(resp, service_.Cancel(id));
        });

    // Rollback: req = (none); resp = errc
    bind.RegisterMethodHandler(kMethodRollback,
        [this](const std::vector<std::uint8_t>&, std::vector<std::uint8_t>& resp) {
            AppendErrc(resp, service_.Rollback());
        });

    // RevertProcessedSwPackages: req = (none); resp = errc
    bind.RegisterMethodHandler(kMethodRevertProcessedSwPackages,
        [this](const std::vector<std::uint8_t>&, std::vector<std::uint8_t>& resp) {
            AppendErrc(resp, service_.RevertProcessedSwPackages());
        });

    // GetSwPackages: req = (none); resp = errc + (ok: u32(count) + [name+version]*)
    bind.RegisterMethodHandler(kMethodGetSwPackages,
        [this](const std::vector<std::uint8_t>&, std::vector<std::uint8_t>& resp) {
            std::vector<ota::SwPackageInfo> packages;
            const ota::UcmErrc errc = service_.GetSwPackages(packages);
            AppendErrc(resp, errc);
            if (errc == ota::UcmErrc::kSuccess) {
                AppendU32(resp, static_cast<std::uint32_t>(packages.size()));
                for (const auto& p : packages) {
                    AppendString(resp, p.name);
                    AppendString(resp, p.version);
                }
            }
        });

    // GetSwClusterInfo: req = (none); resp = errc + (ok: u32(count) + [name+version+state]*)
    bind.RegisterMethodHandler(kMethodGetSwClusterInfo,
        [this](const std::vector<std::uint8_t>&, std::vector<std::uint8_t>& resp) {
            std::vector<ota::SwClusterInfo> clusters;
            const ota::UcmErrc errc = service_.GetSwClusterInfo(clusters);
            AppendErrc(resp, errc);
            if (errc == ota::UcmErrc::kSuccess) {
                AppendU32(resp, static_cast<std::uint32_t>(clusters.size()));
                for (const auto& c : clusters) {
                    AppendString(resp, c.name);
                    AppendString(resp, c.version);
                    AppendString(resp, c.state);
                }
            }
        });

    // GetSwClusterChangeInfo: same shape as GetSwClusterInfo.
    bind.RegisterMethodHandler(kMethodGetSwClusterChangeInfo,
        [this](const std::vector<std::uint8_t>&, std::vector<std::uint8_t>& resp) {
            std::vector<ota::SwClusterInfo> changes;
            const ota::UcmErrc errc = service_.GetSwClusterChangeInfo(changes);
            AppendErrc(resp, errc);
            if (errc == ota::UcmErrc::kSuccess) {
                AppendU32(resp, static_cast<std::uint32_t>(changes.size()));
                for (const auto& c : changes) {
                    AppendString(resp, c.name);
                    AppendString(resp, c.version);
                    AppendString(resp, c.state);
                }
            }
        });

    // GetSwClusterDescription: req = string(name); resp = errc + (ok: string(desc))
    bind.RegisterMethodHandler(kMethodGetSwClusterDescription,
        [this](const std::vector<std::uint8_t>& req, std::vector<std::uint8_t>& resp) {
            std::size_t pos = 0;
            std::string name;
            if (!ReadString(req, pos, name)) {
                AppendErrc(resp, ota::UcmErrc::kOperationNotPermitted);
                return;
            }
            std::string description;
            const ota::UcmErrc errc = service_.GetSwClusterDescription(name, description);
            AppendErrc(resp, errc);
            if (errc == ota::UcmErrc::kSuccess) {
                AppendString(resp, description);
            }
        });

    // GetSwProcessProgress: req = string(id); resp = errc + (ok: string(state) + u32(percent) + string(detail))
    bind.RegisterMethodHandler(kMethodGetSwProcessProgress,
        [this](const std::vector<std::uint8_t>& req, std::vector<std::uint8_t>& resp) {
            std::size_t pos = 0;
            std::string id;
            if (!ReadString(req, pos, id)) {
                AppendErrc(resp, ota::UcmErrc::kOperationNotPermitted);
                return;
            }
            ota::ProgressInfo progress;
            const ota::UcmErrc errc = service_.GetSwProcessProgress(id, progress);
            AppendErrc(resp, errc);
            if (errc == ota::UcmErrc::kSuccess) {
                AppendString(resp, progress.state);
                AppendU32(resp, static_cast<std::uint32_t>(progress.percent));
                AppendString(resp, progress.detail);
            }
        });

    // GetHistory: req = (none); resp = errc + (ok: u32(count) + [from+to+result]*)
    bind.RegisterMethodHandler(kMethodGetHistory,
        [this](const std::vector<std::uint8_t>&, std::vector<std::uint8_t>& resp) {
            std::vector<ota::HistoryEntry> history;
            const ota::UcmErrc errc = service_.GetHistory(history);
            AppendErrc(resp, errc);
            if (errc == ota::UcmErrc::kSuccess) {
                AppendU32(resp, static_cast<std::uint32_t>(history.size()));
                for (const auto& h : history) {
                    AppendString(resp, h.from);
                    AppendString(resp, h.to);
                    AppendString(resp, h.result);
                }
            }
        });

    // GetId: req = (none); resp = errc + (ok: string(id))
    bind.RegisterMethodHandler(kMethodGetId,
        [this](const std::vector<std::uint8_t>&, std::vector<std::uint8_t>& resp) {
            std::string id;
            const ota::UcmErrc errc = service_.GetId(id);
            AppendErrc(resp, errc);
            if (errc == ota::UcmErrc::kSuccess) {
                AppendString(resp, id);
            }
        });
}

}  // namespace ucm
