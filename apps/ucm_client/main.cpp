#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

#include "com/proxy.h"
#include "com/runtime.h"
#include "com/vsomeip/vsomeip_bind_runtime.h"

#include "ota/http_downloader.h"
#include "ota/package_manifest.h"
#include "ota/ucm_types.h"

#include "ucm/ucm_proxy.h"

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: " << argv[0] << " <package_url> [target_version]\n";
        return 1;
    }
    const std::string url = argv[1];
    const std::string version = (argc > 2) ? argv[2] : "1.1.0";

    constexpr com::ServiceIdentifier kServiceId = 0x6001;
    constexpr com::InstanceIdentifier kInstanceId = 0x0001;

    // 1. Download the payload over HTTP (cloud -> Update Agent).
    ota::HttpDownloader downloader;
    const std::string payload_file = "/tmp/ucm_client_package.img";
    std::string error;
    if (!downloader.Download(url, payload_file, error)) {
        std::cerr << "download failed: " << error << "\n";
        return 1;
    }

    std::ifstream in(payload_file, std::ios::binary | std::ios::ate);
    const std::streamsize payload_size = in.tellg();
    in.seekg(0);
    std::vector<std::uint8_t> payload(static_cast<std::size_t>(payload_size));
    in.read(reinterpret_cast<char*>(payload.data()), payload_size);
    in.close();

    // 2. Build the signed package (manifest + payload).
    ota::PackageManifest manifest;
    manifest.name = "platform";
    manifest.version = version;
    manifest.dependencies = {"1.0.0"};
    manifest.signature = ota::HmacSha256Hex(payload);
    const std::vector<std::uint8_t> package = ota::BuildPackage(manifest, payload);
    std::cout << "downloaded " << payload.size() << " bytes, package "
              << package.size() << " bytes\n";

    // 3. Register vsomeip + find the UCM service.
    auto* runtime = com::Runtime::Get();
    runtime->Init();
    auto vsomeip = std::make_unique<com::vsomeip_binding::VsomeipBindRuntime>("ucm-client");
    if (!vsomeip->Init()) {
        std::cerr << "vsomeip init failed\n";
        return 1;
    }
    if (!runtime->RegisterBindRuntime(std::move(vsomeip))) {
        std::cerr << "register bind runtime failed\n";
        return 1;
    }
    auto* bindRuntime = runtime->GetBindRuntime("vsomeip");

    com::FindServiceHandle findHandle{kServiceId, kInstanceId};
    bindRuntime->RegisterFindServiceHandle(findHandle,
        [&](com::ServiceHandleContainer<std::shared_ptr<com::proxy::BindHandle>> const& handles) {
            for (auto const& handle : handles) {
                if (!handle || !handle->IsValid()) {
                    continue;
                }
                ucm::UcmProxy proxy(*handle);

                std::string transfer_id;
                std::uint32_t block_size = 0;
                if (proxy.TransferStart(package.size(), transfer_id, block_size) != ota::UcmErrc::kSuccess) {
                    std::cerr << "TransferStart failed\n";
                    return;
                }

                std::size_t offset = 0;
                std::uint64_t counter = 1;
                while (offset < package.size()) {
                    const std::size_t n = std::min<std::size_t>(block_size, package.size() - offset);
                    const std::vector<std::uint8_t> chunk(package.begin() + offset,
                                                          package.begin() + offset + n);
                    if (proxy.TransferData(transfer_id, chunk, counter) != ota::UcmErrc::kSuccess) {
                        std::cerr << "TransferData failed\n";
                        return;
                    }
                    offset += n;
                    ++counter;
                }

                if (proxy.TransferExit(transfer_id) != ota::UcmErrc::kSuccess) {
                    std::cerr << "TransferExit failed\n";
                    return;
                }
                if (proxy.ProcessSwPackage(transfer_id) != ota::UcmErrc::kSuccess) {
                    std::cerr << "ProcessSwPackage failed\n";
                    return;
                }
                std::cout << "package transferred over SOME-IP, processing started\n";
            }
        });

    std::cout << "waiting for UCM service...\n";
    runtime->Start();  // blocking; triggers the availability handler above.
    return 0;
}
