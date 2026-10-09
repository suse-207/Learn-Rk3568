#include "diag/capi_routine_adapter.h"

#include <chrono>
#include <fstream>
#include <thread>

#include "ota/ota_manager.h"
#include "ota/ota_types.h"
#include "ota/package_manifest.h"

namespace diag {

CapiRoutineAdapter::CapiRoutineAdapter(ota::OtaManager* ota_manager,
                                       std::string download_dir)
    : ota_manager_(ota_manager), download_dir_(std::move(download_dir)) {}

isoft::uds::Result<CapiRoutineAdapter::OperationOutput> CapiRoutineAdapter::Error(
    isoft::uds::server::NrcErrc nrc) {
    return isoft::uds::Result<OperationOutput>::FromError(static_cast<std::int32_t>(nrc));
}

isoft::uds::Result<CapiRoutineAdapter::OperationOutput> CapiRoutineAdapter::Start(
    std::uint16_t routineId,
    std::vector<std::uint8_t> /*requestData*/,
    isoft::uds::server::MetaInfoMap& /*metaInfo*/,
    isoft::uds::server::CancellationHandler /*cancellationHandler*/) {
    constexpr std::uint16_t kOtaStartRoutine = 0x1100U;
    if (routineId != kOtaStartRoutine) {
        return Error(isoft::uds::server::NrcErrc::kRequestOutOfRange);
    }
    if (ota_manager_ == nullptr) {
        return Error(isoft::uds::server::NrcErrc::kConditionsNotCorrect);
    }

    const std::string package_path = download_dir_ + "/ota_download.bin";
    std::ifstream input(package_path, std::ios::binary | std::ios::ate);
    if (!input) {
        return Error(isoft::uds::server::NrcErrc::kConditionsNotCorrect);
    }
    const std::streamsize size = input.tellg();
    input.seekg(0, std::ios::beg);

    std::vector<std::uint8_t> package(static_cast<std::size_t>(size));
    input.read(reinterpret_cast<char*>(package.data()), size);
    input.close();
    if (!input.good() && !input.eof()) {
        return Error(isoft::uds::server::NrcErrc::kConditionsNotCorrect);
    }

    ota::PackageManifest manifest;
    std::vector<std::uint8_t> payload;
    if (!ota::UnpackPackage(package, manifest, payload)) {
        return Error(isoft::uds::server::NrcErrc::kConditionsNotCorrect);
    }

    const std::string payload_path = download_dir_ + "/ota_payload.bin";
    std::ofstream payload_output(payload_path, std::ios::binary | std::ios::trunc);
    if (!payload_output) {
        return Error(isoft::uds::server::NrcErrc::kGeneralProgrammingFailure);
    }
    payload_output.write(reinterpret_cast<const char*>(payload.data()),
                 static_cast<std::streamsize>(payload.size()));
    payload_output.close();

    if (!ota_manager_->start_update(payload_path, manifest.signature, manifest.version)) {
        return Error(isoft::uds::server::NrcErrc::kConditionsNotCorrect);
    }

    while (true) {
        const auto progress = ota_manager_->progress();
        if (progress.state == ota::State::ReadyToSwitch) {
            break;
        }
        if (progress.state == ota::State::Failed ||
            progress.state == ota::State::Rollback) {
            return Error(isoft::uds::server::NrcErrc::kGeneralProgrammingFailure);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    ota::OtaManager* manager = ota_manager_;
    std::thread([manager]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        static_cast<void>(manager->switch_and_reboot());
    }).detach();

    OperationOutput operation_output;
    operation_output.responseData = {};
    return isoft::uds::Result<OperationOutput>::FromValue(operation_output);
}

isoft::uds::Result<CapiRoutineAdapter::OperationOutput> CapiRoutineAdapter::Stop(
    std::uint16_t /*routineId*/,
    std::vector<std::uint8_t> /*requestData*/,
    isoft::uds::server::MetaInfoMap& /*metaInfo*/,
    isoft::uds::server::CancellationHandler /*cancellationHandler*/) {
    return Error(isoft::uds::server::NrcErrc::kSubfunctionNotSupported);
}

isoft::uds::Result<CapiRoutineAdapter::OperationOutput> CapiRoutineAdapter::RequestResults(
    std::uint16_t /*routineId*/,
    std::vector<std::uint8_t> /*requestData*/,
    isoft::uds::server::MetaInfoMap& /*metaInfo*/,
    isoft::uds::server::CancellationHandler /*cancellationHandler*/) {
    return Error(isoft::uds::server::NrcErrc::kSubfunctionNotSupported);
}

}  // namespace diag
