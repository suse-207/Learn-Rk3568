#include "ota/update_agent.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <vector>

#include "ota/http_downloader.h"
#include "ota/package_manifest.h"
#include "ota/ucm_service.h"

namespace ota {

UpdateAgent::UpdateAgent(HttpDownloader& downloader, UcmService& ucm)
    : downloader_(downloader), ucm_(ucm) {}

bool UpdateAgent::Update(const std::string& url, const std::string& version, std::string& error) {
    // 1. Download the payload over HTTP (cloud -> Update Agent).
    const std::string payload_file = "/tmp/update_agent_package.img";
    if (!downloader_.Download(url, payload_file, error)) {
        return false;
    }

    // 2. Read the payload.
    std::ifstream in(payload_file, std::ios::binary | std::ios::ate);
    if (!in) {
        error = "open downloaded package failed";
        return false;
    }
    const std::streamsize payload_size = in.tellg();
    in.seekg(0);
    std::vector<std::uint8_t> payload(static_cast<std::size_t>(payload_size));
    in.read(reinterpret_cast<char*>(payload.data()), payload_size);
    in.close();

    // 3. Build the signed package (manifest + payload).
    PackageManifest manifest;
    manifest.name = "platform";
    manifest.version = version;
    manifest.dependencies = {"1.0.0"};  // Demo: satisfied dependency.
    manifest.signature = HmacSha256Hex(payload);
    const std::vector<std::uint8_t> package = BuildPackage(manifest, payload);

    // 4. TransferStart -> TransferData (block-wise) -> TransferExit.
    std::string transfer_id;
    std::uint32_t block_size = 0;
    if (ucm_.TransferStart(package.size(), transfer_id, block_size) != UcmErrc::kSuccess) {
        error = "TransferStart failed";
        return false;
    }

    std::size_t offset = 0;
    std::uint64_t block_counter = 1;
    while (offset < package.size()) {
        const std::size_t n = std::min<std::size_t>(block_size, package.size() - offset);
        const std::vector<std::uint8_t> chunk(package.begin() + offset, package.begin() + offset + n);
        if (ucm_.TransferData(transfer_id, chunk, block_counter) != UcmErrc::kSuccess) {
            error = "TransferData failed";
            return false;
        }
        offset += n;
        ++block_counter;
    }

    if (ucm_.TransferExit(transfer_id) != UcmErrc::kSuccess) {
        error = "TransferExit failed";
        return false;
    }

    // 5. ProcessSwPackage -> parse manifest, verify signature, install (async).
    if (ucm_.ProcessSwPackage(transfer_id) != UcmErrc::kSuccess) {
        error = "ProcessSwPackage failed";
        return false;
    }

    return true;
}

}  // namespace ota
