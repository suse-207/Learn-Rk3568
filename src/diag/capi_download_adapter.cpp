#include "diag/capi_download_adapter.h"

#include <cerrno>
#include <cstring>

namespace diag {

CapiDownloadAdapter::CapiDownloadAdapter(std::string download_dir)
    : file_path_(download_dir + "/ota_download.bin") {}

isoft::uds::Result<void> CapiDownloadAdapter::Error(isoft::uds::server::NrcErrc nrc) {
    return isoft::uds::Result<void>::FromError(static_cast<std::int32_t>(nrc));
}

isoft::uds::Result<void> CapiDownloadAdapter::RequestDownload(
    std::uint8_t /*dataFormatIdentifier*/,
    std::uint8_t /*addressAndLengthFormatIdentifier*/,
    std::vector<std::uint8_t> /*memoryAddressAndSize*/,
    isoft::uds::server::MetaInfoMap& /*metaInfo*/,
    isoft::uds::server::CancellationHandler /*cancellationHandler*/) noexcept {
    output_.close();
    output_.clear();
    output_.open(file_path_, std::ios::binary | std::ios::trunc);
    if (!output_) {
        return Error(isoft::uds::server::NrcErrc::kConditionsNotCorrect);
    }
    expected_block_ = 0U;
    return {};
}

isoft::uds::Result<void> CapiDownloadAdapter::DownloadData(
    std::uint8_t blockSequenceCounter,
    std::vector<std::uint8_t> transferRequestParameterRecord,
    isoft::uds::server::MetaInfoMap& /*metaInfo*/,
    isoft::uds::server::CancellationHandler /*cancellationHandler*/) noexcept {
    if (!output_) {
        return Error(isoft::uds::server::NrcErrc::kRequestSequenceError);
    }
    if (blockSequenceCounter != expected_block_) {
        return Error(isoft::uds::server::NrcErrc::kWrongBlockSequenceCounter);
    }
    output_.write(reinterpret_cast<const char*>(transferRequestParameterRecord.data()),
                  static_cast<std::streamsize>(transferRequestParameterRecord.size()));
    if (!output_) {
        return Error(isoft::uds::server::NrcErrc::kGeneralProgrammingFailure);
    }
    expected_block_ = static_cast<std::uint8_t>(expected_block_ + 1U);
    return {};
}

isoft::uds::Result<std::vector<std::uint8_t>> CapiDownloadAdapter::RequestDownloadExit(
    std::vector<std::uint8_t> /*transferRequestParameterRecord*/,
    isoft::uds::server::MetaInfoMap& /*metaInfo*/,
    isoft::uds::server::CancellationHandler /*cancellationHandler*/) noexcept {
    if (output_) {
        output_.flush();
        output_.close();
    }
    expected_block_ = 0U;
    return isoft::uds::Result<std::vector<std::uint8_t>>::FromValue({});
}

}  // namespace diag
