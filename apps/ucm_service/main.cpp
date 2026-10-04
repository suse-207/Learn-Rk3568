#include <chrono>
#include <iostream>
#include <thread>

#include "com/runtime.h"
#include "com/skeleton.h"
#include "com/vsomeip/vsomeip_bind_runtime.h"

#include "ota/boot_controller.h"
#include "ota/ota_manager.h"
#include "ota/slot_manager.h"
#include "ota/ucm_service.h"
#include "ota/verify_strategy.h"
#include "ota/version_manager.h"

#include "ucm/ucm_service_skeleton.h"

int main() {
    constexpr com::ServiceIdentifier kServiceId = 0x6001;
    constexpr com::InstanceIdentifier kInstanceId = 0x0001;

    const std::string base = "/tmp/rk3568-ucm-service";

    // The "target UCM" brain: verify / install / A-B switch / rollback.
    ota::MockSlotManager slots(base, "A");
    ota::MockBootController boot(base + "/boot");
    ota::VersionManager versions(base + "/version");
    auto verify = ota::make_verify_strategy("sha256");
    ota::OtaManager manager(*verify, slots, boot, versions, base + "/staging");
    ota::UcmService ucm(base + "/transfer", manager, versions, *verify, "ucm_0");

    // SOME-IP runtime + skeleton.
    auto* runtime = com::Runtime::Get();
    runtime->Init();
    auto vsomeip = std::make_unique<com::vsomeip_binding::VsomeipBindRuntime>("ucm-service");
    if (!vsomeip->Init()) {
        std::cerr << "vsomeip init failed\n";
        return 1;
    }
    if (!runtime->RegisterBindRuntime(std::move(vsomeip))) {
        std::cerr << "register bind runtime failed\n";
        return 1;
    }
    auto* bindRuntime = runtime->GetBindRuntime("vsomeip");

    com::skeleton::ServiceSkeleton skeleton(kServiceId, kInstanceId);
    std::vector<std::unique_ptr<com::skeleton::BindSkeleton>> bindSkeletons;
    bindRuntime->CreateBindSkeleton(skeleton, kInstanceId, bindSkeletons);

    ucm::UcmServiceSkeleton ucm_skeleton(ucm);
    for (auto& bs : bindSkeletons) {
        ucm_skeleton.Register(*bs);
        bs->Offer();
    }
    std::cout << "UCM service offered: service=0x" << std::hex << kServiceId
              << " instance=0x" << kInstanceId << std::dec << "\n";

    // Background thread: poll OtaManager progress and finish the A-B switch.
    std::thread progress_thread([&manager, &slots, &versions]() {
        while (true) {
            const ota::Progress p = manager.progress();
            if (p.state == ota::State::ReadyToSwitch) {
                std::cout << "ready-to-switch, activating...\n";
                manager.switch_and_reboot();
                manager.report_boot_result(true);
                std::cout << "final: slot=" << slots.current_slot()
                          << " version=" << versions.current_version() << "\n";
                break;
            }
            if (p.state == ota::State::Failed) {
                std::cerr << "update failed: " << p.detail << "\n";
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    });

    runtime->Start();  // blocking; receives SOME-IP TransferData.

    progress_thread.join();
    return 0;
}
