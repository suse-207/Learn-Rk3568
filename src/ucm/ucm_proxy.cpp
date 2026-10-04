#include "ucm/ucm_proxy.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "com/proxy.h"
#include "ucm/ucm_protocol.h"

namespace ucm {

namespace {

ota::UcmErrc ReadErrc(const std::vector<std::uint8_t>& data, std::size_t& pos) {
    std::uint32_t v = static_cast<std::uint32_t>(ota::UcmErrc::kOperationNotPermitted);
    if (!ReadU32(data, pos, v)) {
        return ota::UcmErrc::kOperationNotPermitted;
    }
    return static_cast<ota::UcmErrc>(v);
}

}  // namespace

ota::UcmErrc UcmProxy::TransferStart(std::uint64_t size,
                                     std::string& out_transfer_id,
                                     std::uint32_t& out_block_size) {
    std::vector<std::uint8_t> req;
    AppendU64(req, size);

    auto resp = handle_.SendRequest(kMethodTransferStart, req, std::chrono::seconds(5));
    if (!resp) {
        return ota::UcmErrc::kOperationNotPermitted;
    }
    const std::vector<std::uint8_t>& data = resp.Value();
    std::size_t pos = 0;
    const ota::UcmErrc errc = ReadErrc(data, pos);
    if (errc != ota::UcmErrc::kSuccess) {
        return errc;
    }
    if (!ReadString(data, pos, out_transfer_id) || !ReadU32(data, pos, out_block_size)) {
        return ota::UcmErrc::kOperationNotPermitted;
    }
    return ota::UcmErrc::kSuccess;
}

ota::UcmErrc UcmProxy::TransferData(const std::string& transfer_id,
                                    const std::vector<std::uint8_t>& block,
                                    std::uint64_t block_counter) {
    std::vector<std::uint8_t> req;
    AppendString(req, transfer_id);
    AppendU64(req, block_counter);
    AppendBytes(req, block);

    auto resp = handle_.SendRequest(kMethodTransferData, req, std::chrono::seconds(5));
    if (!resp) {
        return ota::UcmErrc::kOperationNotPermitted;
    }
    const std::vector<std::uint8_t>& data = resp.Value();
    std::size_t pos = 0;
    return ReadErrc(data, pos);
}

ota::UcmErrc UcmProxy::TransferExit(const std::string& transfer_id) {
    std::vector<std::uint8_t> req;
    AppendString(req, transfer_id);

    auto resp = handle_.SendRequest(kMethodTransferExit, req, std::chrono::seconds(5));
    if (!resp) {
        return ota::UcmErrc::kOperationNotPermitted;
    }
    const std::vector<std::uint8_t>& data = resp.Value();
    std::size_t pos = 0;
    return ReadErrc(data, pos);
}

ota::UcmErrc UcmProxy::DeleteTransfer(const std::string& transfer_id) {
    std::vector<std::uint8_t> req;
    AppendString(req, transfer_id);

    auto resp = handle_.SendRequest(kMethodDeleteTransfer, req, std::chrono::seconds(5));
    if (!resp) {
        return ota::UcmErrc::kOperationNotPermitted;
    }
    const std::vector<std::uint8_t>& data = resp.Value();
    std::size_t pos = 0;
    return ReadErrc(data, pos);
}

ota::UcmErrc UcmProxy::ProcessSwPackage(const std::string& transfer_id) {
    std::vector<std::uint8_t> req;
    AppendString(req, transfer_id);

    auto resp = handle_.SendRequest(kMethodProcessSwPackage, req, std::chrono::seconds(5));
    if (!resp) {
        return ota::UcmErrc::kOperationNotPermitted;
    }
    const std::vector<std::uint8_t>& data = resp.Value();
    std::size_t pos = 0;
    return ReadErrc(data, pos);
}

ota::UcmErrc UcmProxy::Activate() {
    std::vector<std::uint8_t> req;
    auto resp = handle_.SendRequest(kMethodActivate, req, std::chrono::seconds(5));
    if (!resp) {
        return ota::UcmErrc::kOperationNotPermitted;
    }
    std::size_t pos = 0;
    return ReadErrc(resp.Value(), pos);
}

ota::UcmErrc UcmProxy::Finish() {
    std::vector<std::uint8_t> req;
    auto resp = handle_.SendRequest(kMethodFinish, req, std::chrono::seconds(5));
    if (!resp) {
        return ota::UcmErrc::kOperationNotPermitted;
    }
    std::size_t pos = 0;
    return ReadErrc(resp.Value(), pos);
}

ota::UcmErrc UcmProxy::Cancel(const std::string& transfer_id) {
    std::vector<std::uint8_t> req;
    AppendString(req, transfer_id);
    auto resp = handle_.SendRequest(kMethodCancel, req, std::chrono::seconds(5));
    if (!resp) {
        return ota::UcmErrc::kOperationNotPermitted;
    }
    std::size_t pos = 0;
    return ReadErrc(resp.Value(), pos);
}

ota::UcmErrc UcmProxy::Rollback() {
    std::vector<std::uint8_t> req;
    auto resp = handle_.SendRequest(kMethodRollback, req, std::chrono::seconds(5));
    if (!resp) {
        return ota::UcmErrc::kOperationNotPermitted;
    }
    std::size_t pos = 0;
    return ReadErrc(resp.Value(), pos);
}

ota::UcmErrc UcmProxy::RevertProcessedSwPackages() {
    std::vector<std::uint8_t> req;
    auto resp = handle_.SendRequest(kMethodRevertProcessedSwPackages, req, std::chrono::seconds(5));
    if (!resp) {
        return ota::UcmErrc::kOperationNotPermitted;
    }
    std::size_t pos = 0;
    return ReadErrc(resp.Value(), pos);
}

ota::UcmErrc UcmProxy::GetId(std::string& id) {
    std::vector<std::uint8_t> req;
    auto resp = handle_.SendRequest(kMethodGetId, req, std::chrono::seconds(5));
    if (!resp) {
        return ota::UcmErrc::kOperationNotPermitted;
    }
    const std::vector<std::uint8_t>& data = resp.Value();
    std::size_t pos = 0;
    const ota::UcmErrc errc = ReadErrc(data, pos);
    if (errc != ota::UcmErrc::kSuccess) {
        return errc;
    }
    if (!ReadString(data, pos, id)) {
        return ota::UcmErrc::kOperationNotPermitted;
    }
    return ota::UcmErrc::kSuccess;
}

ota::UcmErrc UcmProxy::GetSwPackages(std::vector<ota::SwPackageInfo>& packages) {
    std::vector<std::uint8_t> req;
    auto resp = handle_.SendRequest(kMethodGetSwPackages, req, std::chrono::seconds(5));
    if (!resp) {
        return ota::UcmErrc::kOperationNotPermitted;
    }
    const std::vector<std::uint8_t>& data = resp.Value();
    std::size_t pos = 0;
    const ota::UcmErrc errc = ReadErrc(data, pos);
    if (errc != ota::UcmErrc::kSuccess) {
        return errc;
    }
    std::uint32_t count = 0;
    if (!ReadU32(data, pos, count)) {
        return ota::UcmErrc::kOperationNotPermitted;
    }
    packages.clear();
    for (std::uint32_t i = 0; i < count; ++i) {
        ota::SwPackageInfo p;
        if (!ReadString(data, pos, p.name) || !ReadString(data, pos, p.version)) {
            return ota::UcmErrc::kOperationNotPermitted;
        }
        packages.push_back(std::move(p));
    }
    return ota::UcmErrc::kSuccess;
}

ota::UcmErrc UcmProxy::GetSwClusterInfo(std::vector<ota::SwClusterInfo>& clusters) {
    std::vector<std::uint8_t> req;
    auto resp = handle_.SendRequest(kMethodGetSwClusterInfo, req, std::chrono::seconds(5));
    if (!resp) {
        return ota::UcmErrc::kOperationNotPermitted;
    }
    const std::vector<std::uint8_t>& data = resp.Value();
    std::size_t pos = 0;
    const ota::UcmErrc errc = ReadErrc(data, pos);
    if (errc != ota::UcmErrc::kSuccess) {
        return errc;
    }
    std::uint32_t count = 0;
    if (!ReadU32(data, pos, count)) {
        return ota::UcmErrc::kOperationNotPermitted;
    }
    clusters.clear();
    for (std::uint32_t i = 0; i < count; ++i) {
        ota::SwClusterInfo c;
        if (!ReadString(data, pos, c.name) || !ReadString(data, pos, c.version) || !ReadString(data, pos, c.state)) {
            return ota::UcmErrc::kOperationNotPermitted;
        }
        clusters.push_back(std::move(c));
    }
    return ota::UcmErrc::kSuccess;
}

ota::UcmErrc UcmProxy::GetSwClusterChangeInfo(std::vector<ota::SwClusterInfo>& changes) {
    std::vector<std::uint8_t> req;
    auto resp = handle_.SendRequest(kMethodGetSwClusterChangeInfo, req, std::chrono::seconds(5));
    if (!resp) {
        return ota::UcmErrc::kOperationNotPermitted;
    }
    const std::vector<std::uint8_t>& data = resp.Value();
    std::size_t pos = 0;
    const ota::UcmErrc errc = ReadErrc(data, pos);
    if (errc != ota::UcmErrc::kSuccess) {
        return errc;
    }
    std::uint32_t count = 0;
    if (!ReadU32(data, pos, count)) {
        return ota::UcmErrc::kOperationNotPermitted;
    }
    changes.clear();
    for (std::uint32_t i = 0; i < count; ++i) {
        ota::SwClusterInfo c;
        if (!ReadString(data, pos, c.name) || !ReadString(data, pos, c.version) || !ReadString(data, pos, c.state)) {
            return ota::UcmErrc::kOperationNotPermitted;
        }
        changes.push_back(std::move(c));
    }
    return ota::UcmErrc::kSuccess;
}

ota::UcmErrc UcmProxy::GetSwClusterDescription(const std::string& name, std::string& description) {
    std::vector<std::uint8_t> req;
    AppendString(req, name);
    auto resp = handle_.SendRequest(kMethodGetSwClusterDescription, req, std::chrono::seconds(5));
    if (!resp) {
        return ota::UcmErrc::kOperationNotPermitted;
    }
    const std::vector<std::uint8_t>& data = resp.Value();
    std::size_t pos = 0;
    const ota::UcmErrc errc = ReadErrc(data, pos);
    if (errc != ota::UcmErrc::kSuccess) {
        return errc;
    }
    if (!ReadString(data, pos, description)) {
        return ota::UcmErrc::kOperationNotPermitted;
    }
    return ota::UcmErrc::kSuccess;
}

ota::UcmErrc UcmProxy::GetSwProcessProgress(const std::string& transfer_id, ota::ProgressInfo& progress) {
    std::vector<std::uint8_t> req;
    AppendString(req, transfer_id);
    auto resp = handle_.SendRequest(kMethodGetSwProcessProgress, req, std::chrono::seconds(5));
    if (!resp) {
        return ota::UcmErrc::kOperationNotPermitted;
    }
    const std::vector<std::uint8_t>& data = resp.Value();
    std::size_t pos = 0;
    const ota::UcmErrc errc = ReadErrc(data, pos);
    if (errc != ota::UcmErrc::kSuccess) {
        return errc;
    }
    std::uint32_t percent = 0;
    if (!ReadString(data, pos, progress.state) || !ReadU32(data, pos, percent) || !ReadString(data, pos, progress.detail)) {
        return ota::UcmErrc::kOperationNotPermitted;
    }
    progress.percent = static_cast<int>(percent);
    return ota::UcmErrc::kSuccess;
}

ota::UcmErrc UcmProxy::GetHistory(std::vector<ota::HistoryEntry>& history) {
    std::vector<std::uint8_t> req;
    auto resp = handle_.SendRequest(kMethodGetHistory, req, std::chrono::seconds(5));
    if (!resp) {
        return ota::UcmErrc::kOperationNotPermitted;
    }
    const std::vector<std::uint8_t>& data = resp.Value();
    std::size_t pos = 0;
    const ota::UcmErrc errc = ReadErrc(data, pos);
    if (errc != ota::UcmErrc::kSuccess) {
        return errc;
    }
    std::uint32_t count = 0;
    if (!ReadU32(data, pos, count)) {
        return ota::UcmErrc::kOperationNotPermitted;
    }
    history.clear();
    for (std::uint32_t i = 0; i < count; ++i) {
        ota::HistoryEntry h;
        if (!ReadString(data, pos, h.from) || !ReadString(data, pos, h.to) || !ReadString(data, pos, h.result)) {
            return ota::UcmErrc::kOperationNotPermitted;
        }
        history.push_back(std::move(h));
    }
    return ota::UcmErrc::kSuccess;
}

}  // namespace ucm
