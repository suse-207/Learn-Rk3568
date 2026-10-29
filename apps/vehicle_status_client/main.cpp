#include <atomic>
#include <chrono>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "com/bind_runtime_factory.h"
#include "com/proxy.h"
#include "com/runtime.h"

#include "ota/verify_strategy.h"

#include "vehicle_status/vehicle_status_protocol.h"
#include "vehicle_status/vehicle_status_proxy.h"

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: " << argv[0]
                  << " <image_path> [version]\n";
        return 1;
    }

    const std::string image_path = argv[1];
    const std::string version = (argc > 2) ? argv[2] : "0.2.0";

    std::ifstream file(image_path, std::ios::binary | std::ios::ate);
    if (!file) {
        std::cerr << "open image failed: " << image_path << "\n";
        return 1;
    }
    const std::uint64_t package_size =
        static_cast<std::uint64_t>(file.tellg());
    file.close();

    auto verify = ota::make_verify_strategy("sha256");
    std::string digest_error;
    const std::string sha256 = verify->digest(image_path, digest_error);
    if (sha256.empty()) {
        std::cerr << "digest failed: " << digest_error << "\n";
        return 1;
    }

    auto* runtime = com::Runtime::Get();
    runtime->Init();

    auto createdRuntime =
        com::CreateBindRuntime("vsomeip", "vehicle-status-client");
    if (!createdRuntime) {
        std::cerr << "create bind runtime failed\n";
        return 1;
    }
    if (!runtime->RegisterBindRuntime(std::move(createdRuntime))) {
        std::cerr << "register bind runtime failed\n";
        return 1;
    }

    auto* bindRuntime = runtime->GetBindRuntime("vsomeip");
    if (!bindRuntime) {
        std::cerr << "get bind runtime failed\n";
        return 1;
    }

    com::FindServiceHandle findHandle{
        vehicle_status::kServiceId,
        vehicle_status::kInstanceId};

    std::atomic<bool> started{false};

    bindRuntime->RegisterFindServiceHandle(
        findHandle,
        [bindRuntime, &runtime, &started, version, package_size, sha256](
            com::ServiceHandleContainer<
                std::shared_ptr<com::proxy::BindHandle>> const& handles) {
            if (started.exchange(true)) {
                return;
            }

            auto copied = handles;
            std::thread([bindRuntime, &runtime, copied,
                         version, package_size, sha256]() {
                for (auto const& handle : copied) {
                    if (!handle || !handle->IsValid()) {
                        continue;
                    }

                    vehicle_status::VehicleStatusProxy proxy(*handle);

                    std::string current_version;
                    std::string current_slot;
                    vehicle_status::OtaState state =
                        vehicle_status::OtaState::kIdle;

                    auto errc = proxy.GetStatus(
                        current_version, current_slot, state);
                    std::cout << "GetStatus errc="
                              << static_cast<std::uint32_t>(errc)
                              << " version=" << current_version
                              << " slot=" << current_slot
                              << " state=" << static_cast<int>(state) << "\n";

                    std::atomic<int> notifications{0};
                    proxy.SubscribeOtaStateChanged(
                        [&notifications](vehicle_status::OtaState s,
                                         std::uint32_t p,
                                         const std::string& d) {
                            std::cout << "[client] <- NOTIFY OtaStateChanged"
                                      << " state=" << static_cast<int>(s)
                                      << " percent=" << p
                                      << " detail=" << d << "\n";
                            notifications.fetch_add(1);
                        });
                    std::cout << "[client] subscribed to eventgroup 0x"
                              << std::hex << vehicle_status::kEventGroupId
                              << std::dec << ", waiting for ACK ...\n";
                    std::this_thread::sleep_for(
                        std::chrono::milliseconds(1000));

                    std::string update_id;
                    errc = proxy.StartUpdate(
                        version, package_size, sha256, update_id);
                    std::cout << "StartUpdate errc="
                              << static_cast<std::uint32_t>(errc)
                              << " update_id=" << update_id << "\n";

                    std::uint32_t percent = 0;
                    std::string detail;
                    errc = proxy.GetProgress(
                        update_id, state, percent, detail);
                    std::cout << "GetProgress errc="
                              << static_cast<std::uint32_t>(errc)
                              << " state=" << static_cast<int>(state)
                              << " percent=" << percent
                              << " detail=" << detail << "\n";

                    std::cout << "[client] waiting for notifications ...\n";
                    std::this_thread::sleep_for(
                        std::chrono::milliseconds(3000));
                    std::cout << "[client] received "
                              << notifications.load()
                              << " notifications\n";

                    errc = proxy.Rollback();
                    std::cout << "Rollback errc="
                              << static_cast<std::uint32_t>(errc) << "\n";
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(300));
                runtime->Stop();
            }).detach();
        });

    std::cout << "waiting for VehicleStatus service...\n";
    runtime->Start();
    runtime->Deinit();
    return 0;
}
