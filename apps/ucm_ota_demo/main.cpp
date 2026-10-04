#include <chrono>
#include <iostream>
#include <thread>

#include "ota/boot_controller.h"
#include "ota/http_downloader.h"
#include "ota/ota_manager.h"
#include "ota/slot_manager.h"
#include "ota/ucm_service.h"
#include "ota/update_agent.h"
#include "ota/verify_strategy.h"
#include "ota/version_manager.h"

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: " << argv[0] << " <package_url> [target_version]\n";
        return 1;
    }
    const std::string url = argv[1];
    const std::string version = (argc > 2) ? argv[2] : "1.1.0";

    const std::string base = "/tmp/rk3568-ucm-demo";

    ota::MockSlotManager slots(base, "A");
    ota::MockBootController boot(base + "/boot");
    ota::VersionManager versions(base + "/version");
    auto verify = ota::make_verify_strategy("sha256");

    ota::OtaManager manager(*verify, slots, boot, versions, base + "/staging");
    ota::UcmService ucm(base + "/transfer", manager, versions, *verify, "ucm_0");
    ota::HttpDownloader downloader;
    ota::UpdateAgent agent(downloader, ucm);

    std::cout << "start: current_slot=" << slots.current_slot()
              << " current_version=" << versions.current_version() << "\n";

    std::string error;
    if (!agent.Update(url, version, error)) {
        std::cerr << "update failed: " << error << "\n";
        return 1;
    }

    // Poll the OtaManager until ready-to-switch or failed.
    while (true) {
        const ota::Progress p = manager.progress();
        std::cout << "state=" << ota::state_to_string(p.state)
                  << " percent=" << p.percent
                  << " detail=" << p.detail << "\n";
        if (p.state == ota::State::ReadyToSwitch || p.state == ota::State::Failed) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    if (manager.progress().state != ota::State::ReadyToSwitch) {
        std::cerr << "update did not reach ready_to_switch\n";
        return 1;
    }

    if (!manager.switch_and_reboot()) {
        std::cerr << "switch_and_reboot failed\n";
        return 1;
    }
    if (!manager.report_boot_result(true)) {
        std::cerr << "report_boot_result failed\n";
        return 1;
    }

    std::cout << "final: current_slot=" << slots.current_slot()
              << " current_version=" << versions.current_version() << "\n";
    return 0;
}
