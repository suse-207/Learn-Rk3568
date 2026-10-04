#include "ota/ucm_service.h"

#include <fstream>
#include <utility>

#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

namespace ota {

namespace {
constexpr std::uint32_t kBlockSize = 4096;
constexpr const char* kPackageName = "platform";
constexpr const char* kClusterName = "app";
constexpr const char* kStateActive = "kActive";
constexpr const char* kStatePending = "kPending";
}

UcmService::UcmService(std::string transfer_dir,
                       OtaManager& manager,
                       VersionManager& versions,
                       VerifyStrategy& verify,
                       std::string ucm_id)
    : transfer_dir_(std::move(transfer_dir)),
      manager_(manager),
      versions_(versions),
      verify_(verify),
      ucm_id_(std::move(ucm_id)) {
    ::mkdir(transfer_dir_.c_str(), 0755);
}

std::string UcmService::PackagePath(const std::string& transfer_id) const {
    return transfer_dir_ + "/" + transfer_id + ".zip";
}

std::string UcmService::MetaPath(const std::string& transfer_id) const {
    return transfer_dir_ + "/" + transfer_id + ".meta";
}

void UcmService::SaveSession(const std::string& transfer_id, const Session& session) {
    std::ofstream out(MetaPath(transfer_id), std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(&session.expected_bytes), sizeof(session.expected_bytes));
    out.write(reinterpret_cast<const char*>(&session.received_bytes), sizeof(session.received_bytes));
    out.write(reinterpret_cast<const char*>(&session.expected_block), sizeof(session.expected_block));
}

bool UcmService::LoadSession(const std::string& transfer_id, Session& session) {
    std::ifstream in(MetaPath(transfer_id), std::ios::binary);
    if (!in) {
        return false;
    }
    in.read(reinterpret_cast<char*>(&session.expected_bytes), sizeof(session.expected_bytes));
    in.read(reinterpret_cast<char*>(&session.received_bytes), sizeof(session.received_bytes));
    in.read(reinterpret_cast<char*>(&session.expected_block), sizeof(session.expected_block));
    return !in.fail();
}

void UcmService::RemoveSessionFiles(const std::string& transfer_id) {
    ::unlink(PackagePath(transfer_id).c_str());
    ::unlink(MetaPath(transfer_id).c_str());
}

// -- transfer --

UcmErrc UcmService::TransferStart(std::uint64_t size,
                                  std::string& out_transfer_id,
                                  std::uint32_t& out_block_size) {
    // Check free disk space (like ara::ucm's GetFreeDiskSpace).
    struct statvfs vfs;
    if (::statvfs(transfer_dir_.c_str(), &vfs) == 0) {
        const std::uint64_t available =
            static_cast<std::uint64_t>(vfs.f_bavail) * static_cast<std::uint64_t>(vfs.f_frsize);
        if (size > available) {
            return UcmErrc::kInsufficientMemory;
        }
    }

    const std::string id = std::to_string(next_id_++);

    std::ofstream file(PackagePath(id), std::ios::binary | std::ios::trunc);
    if (!file) {
        return UcmErrc::kInsufficientMemory;
    }
    file.close();

    Session session;
    session.expected_bytes = size;
    sessions_[id] = session;
    SaveSession(id, session);

    out_transfer_id = id;
    out_block_size = kBlockSize;
    return UcmErrc::kSuccess;
}

UcmErrc UcmService::TransferData(const std::string& transfer_id,
                                 const std::vector<std::uint8_t>& block,
                                 std::uint64_t block_counter) {
    auto it = sessions_.find(transfer_id);
    if (it == sessions_.end()) {
        // Resume from persisted state (e.g. after a restart).
        Session resumed;
        if (!LoadSession(transfer_id, resumed)) {
            return UcmErrc::kInvalidTransferId;
        }
        sessions_[transfer_id] = resumed;
        it = sessions_.find(transfer_id);
    }
    Session& session = it->second;

    if (block_counter != session.expected_block) {
        return UcmErrc::kIncorrectBlock;
    }
    if (session.received_bytes + block.size() > session.expected_bytes) {
        return UcmErrc::kIncorrectSize;
    }

    std::ofstream file(PackagePath(transfer_id), std::ios::binary | std::ios::app);
    if (!file) {
        return UcmErrc::kInsufficientMemory;
    }
    file.write(reinterpret_cast<const char*>(block.data()),
               static_cast<std::streamsize>(block.size()));
    file.close();

    session.expected_block++;
    session.received_bytes += block.size();
    SaveSession(transfer_id, session);
    return UcmErrc::kSuccess;
}

UcmErrc UcmService::TransferExit(const std::string& transfer_id) {
    auto it = sessions_.find(transfer_id);
    if (it == sessions_.end()) {
        return UcmErrc::kInvalidTransferId;
    }
    const Session& session = it->second;
    if (session.received_bytes == 0) {
        return UcmErrc::kOperationNotPermitted;
    }
    if (session.received_bytes != session.expected_bytes) {
        return UcmErrc::kInsufficientData;
    }
    return UcmErrc::kSuccess;
}

UcmErrc UcmService::DeleteTransfer(const std::string& transfer_id) {
    auto it = sessions_.find(transfer_id);
    if (it == sessions_.end()) {
        return UcmErrc::kInvalidTransferId;
    }
    sessions_.erase(it);
    RemoveSessionFiles(transfer_id);
    return UcmErrc::kSuccess;
}

// -- process / lifecycle --

UcmErrc UcmService::ProcessSwPackage(const std::string& transfer_id) {
    auto it = sessions_.find(transfer_id);
    if (it == sessions_.end()) {
        return UcmErrc::kInvalidTransferId;
    }

    // Read the transferred package.
    std::ifstream in(PackagePath(transfer_id), std::ios::binary | std::ios::ate);
    if (!in) {
        return UcmErrc::kInvalidPackageManifest;
    }
    const std::streamsize size = in.tellg();
    in.seekg(0);
    std::vector<std::uint8_t> pkg(static_cast<std::size_t>(size));
    in.read(reinterpret_cast<char*>(pkg.data()), size);
    in.close();

    // Unpack manifest + payload.
    PackageManifest manifest;
    std::vector<std::uint8_t> payload;
    if (!UnpackPackage(pkg, manifest, payload)) {
        return UcmErrc::kInvalidPackageManifest;
    }

    // Verify the signature (HMAC over the payload).
    if (manifest.signature != HmacSha256Hex(payload)) {
        return UcmErrc::kAuthenticationFailed;
    }

    // Version check.
    if (manifest.version == versions_.current_version()) {
        return UcmErrc::kIncompatiblePackageVersion;
    }

    // Dependency check (demo: every dependency must equal the current version).
    for (const auto& dep : manifest.dependencies) {
        if (dep != versions_.current_version()) {
            return UcmErrc::kMissingDependencies;
        }
    }

    // Write the payload to a file and hand it to the OtaManager.
    const std::string payload_path = PackagePath(transfer_id) + ".img";
    std::ofstream out(payload_path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(payload.data()),
              static_cast<std::streamsize>(payload.size()));
    out.close();

    std::string error;
    const std::string digest = verify_.digest(payload_path, error);
    if (digest.empty()) {
        return UcmErrc::kInsufficientMemory;
    }

    if (!manager_.start_update(payload_path, digest, manifest.version)) {
        return UcmErrc::kServiceBusy;
    }

    sessions_.erase(it);
    RemoveSessionFiles(transfer_id);  // .zip + .meta consumed; .img kept for OtaManager.
    return UcmErrc::kSuccess;
}

UcmErrc UcmService::Activate() {
    return manager_.switch_and_reboot() ? UcmErrc::kSuccess : UcmErrc::kOperationNotPermitted;
}

UcmErrc UcmService::Finish() {
    return manager_.report_boot_result(true) ? UcmErrc::kSuccess : UcmErrc::kOperationNotPermitted;
}

UcmErrc UcmService::Rollback() {
    return manager_.rollback() ? UcmErrc::kSuccess : UcmErrc::kOperationNotPermitted;
}

UcmErrc UcmService::Cancel(const std::string& transfer_id) {
    // Demo: cancel is equivalent to discarding a pending transfer.
    return DeleteTransfer(transfer_id);
}

UcmErrc UcmService::RevertProcessedSwPackages() {
    // Demo: no separate "processed but not activated" state to revert.
    return UcmErrc::kSuccess;
}

// -- query --

UcmErrc UcmService::GetId(std::string& id) const {
    id = ucm_id_;
    return UcmErrc::kSuccess;
}

UcmErrc UcmService::GetSwPackages(std::vector<SwPackageInfo>& packages) const {
    packages.clear();
    packages.push_back({kPackageName, versions_.current_version()});
    return UcmErrc::kSuccess;
}

UcmErrc UcmService::GetSwClusterInfo(std::vector<SwClusterInfo>& clusters) const {
    clusters.clear();
    clusters.push_back({kClusterName, versions_.current_version(), kStateActive});
    return UcmErrc::kSuccess;
}

UcmErrc UcmService::GetSwClusterChangeInfo(std::vector<SwClusterInfo>& changes) const {
    changes.clear();
    if (versions_.target_version() != versions_.current_version()) {
        changes.push_back({kClusterName, versions_.target_version(), kStatePending});
    }
    return UcmErrc::kSuccess;
}

UcmErrc UcmService::GetSwClusterDescription(const std::string& name, std::string& description) const {
    description = "demo software cluster: " + name;
    return UcmErrc::kSuccess;
}

UcmErrc UcmService::GetSwProcessProgress(const std::string& /*transfer_id*/, ProgressInfo& progress) const {
    const ota::Progress p = manager_.progress();
    progress.state = state_to_string(p.state);
    progress.percent = p.percent;
    progress.detail = p.detail;
    return UcmErrc::kSuccess;
}

UcmErrc UcmService::GetHistory(std::vector<HistoryEntry>& history) const {
    history.clear();
    for (const auto& r : versions_.history()) {
        history.push_back({r.from, r.to, r.result});
    }
    return UcmErrc::kSuccess;
}

}  // namespace ota
