#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "com/bind_runtime_factory.h"
#include "com/runtime.h"
#include "com/skeleton.h"

#include "ota/boot_controller.h"
#include "ota/linuxab_boot_controller.h"
#include "ota/linuxab_slot_manager.h"
#include "ota/ota_manager.h"
#include "ota/slot_manager.h"
#include "ota/verify_strategy.h"
#include "ota/version_manager.h"

#include "vehicle_status/ota_vehicle_status_service.h"
#include "vehicle_status/vehicle_status_protocol.h"
#include "vehicle_status/vehicle_status_service_skeleton.h"

int main(int argc, char** argv) {
    std::string image_path = "/tmp/vehicle_status_ota.bin";
    if (argc > 1) {
        image_path = argv[1];
    }

    const std::string base = "/tmp/rk3568-vehicle-status-ota";

    std::unique_ptr<ota::SlotManager> slots;
    std::unique_ptr<ota::BootController> boot;
    const char* backend = std::getenv("RK_OTA_BACKEND");
    const bool linuxab = backend != nullptr && std::string(backend) == "linuxab";
    if (linuxab) {
        slots = std::make_unique<ota::LinuxAbSlotManager>();
        boot = std::make_unique<ota::LinuxAbBootController>();
    } else {
        slots = std::make_unique<ota::MockSlotManager>(base, "A");
        boot = std::make_unique<ota::MockBootController>(base + "/boot");
    }

    ota::VersionManager versions(base + "/version");
    versions.set_current("0.1.0");
    auto verify = ota::make_verify_strategy("sha256");
    ota::OtaManager manager(*verify, *slots, *boot, versions, base + "/staging", true);

    vehicle_status::OtaVehicleStatusService service(
        manager, *slots, versions, image_path);
    vehicle_status::VehicleStatusServiceSkeleton skeleton(service);

    auto* runtime = com::Runtime::Get();
    runtime->Init();

    auto createdRuntime =
        com::CreateBindRuntime("vsomeip", "vehicle-status-service");
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

    com::skeleton::ServiceSkeleton serviceSkeleton(
        vehicle_status::kServiceId,
        vehicle_status::kInstanceId);

    std::vector<std::unique_ptr<com::skeleton::BindSkeleton>> bindSkeletons;
    bindRuntime->CreateBindSkeleton(
        serviceSkeleton, vehicle_status::kInstanceId, bindSkeletons);

    for (auto& bs : bindSkeletons) {
        skeleton.Register(*bs);
        bs->Offer();
    }

    std::cout << "VehicleStatus service offered, backend="
              << (linuxab ? "linuxab" : "mock")
              << ", image_path=" << image_path << "\n";

    runtime->Start();

    for (auto& bs : bindSkeletons) {
        bs->StopOffer();
    }

    runtime->Stop();
    runtime->Deinit();
    return 0;
}
