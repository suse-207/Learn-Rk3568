#include <chrono>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include "ota/boot_controller.h"
#include "ota/ota_manager.h"
#include "ota/slot_manager.h"
#include "ota/verify_strategy.h"
#include "ota/version_manager.h"

namespace {

std::string make_image(const std::string& path, const std::string& text) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << text;
    return path;
}

void print_progress(const ota::Progress& progress) {
    std::cout << "state=" << ota::state_to_string(progress.state)
              << " percent=" << progress.percent
              << " detail=" << progress.detail << "\n";
}

}  // namespace

int main(int argc, char** argv) {
    const bool fail_boot = argc > 1 && std::string(argv[1]) == "--fail-boot";
    const std::string base = "/tmp/rk3568-ota-demo";
    const std::string image = "/tmp/rk3568-ota-image.bin";

    make_image(image, "OTA-1.1.0-image\n");

    ota::MockSlotManager slots(base, "A");
    ota::MockBootController boot(base + "/boot");
    ota::VersionManager versions(base + "/version");
    auto verify = ota::make_verify_strategy("sha256");

    std::string digest_error;
    const std::string digest = verify->digest(image, digest_error);
    if (digest.empty()) {
        std::cerr << "digest failed: " << digest_error << "\n";
        return 1;
    }

    std::cout << "start: current_slot=" << slots.current_slot()
              << " current_version=" << versions.current_version() << "\n";
    std::cout << "sha256=" << digest << "\n";

    ota::OtaManager manager(*verify, slots, boot, versions, base + "/staging");
    if (!manager.start_update(image, digest, "1.1.0")) {
        std::cerr << "start_update failed\n";
        return 1;
    }

    while (true) {
        const auto progress = manager.progress();
        print_progress(progress);
        if (progress.state == ota::State::ReadyToSwitch ||
            progress.state == ota::State::Failed) {
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
    std::cout << "boot.next_slot=" << boot.next_slot()
              << " reboot_attempted=" << boot.reboot_attempted() << "\n";

    if (!manager.report_boot_result(!fail_boot, "simulated health check timeout")) {
        std::cerr << "report_boot_result failed\n";
        return 1;
    }

    print_progress(manager.progress());
    std::cout << "final: current_slot=" << slots.current_slot()
              << " current_version=" << versions.current_version()
              << " target_version=" << versions.target_version() << "\n";

    std::cout << "history:\n";
    for (const auto& record : versions.history()) {
        std::cout << "  " << record.from << " -> " << record.to
                  << " " << record.result << "\n";
    }
    return 0;
}

