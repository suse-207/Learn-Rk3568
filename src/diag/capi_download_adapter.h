#pragma once

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#include "isoft/uds/result.h"
#include "isoft/uds/transfer_managment/download.h"
#include "isoft/uds/uds_nrc_error_domain.h"

namespace diag {

class CapiDownloadAdapter final : public isoft::uds::server::DownloadInterface {
public:
    explicit CapiDownloadAdapter(std::string download_dir);

    isoft::uds::Result<void> RequestDownload(
        std::uint8_t dataFormatIdentifier,
        std::uint8_t addressAndLengthFormatIdentifier,
        std::vector<std::uint8_t> memoryAddressAndSize,
        isoft::uds::server::MetaInfoMap& metaInfo,
        isoft::uds::server::CancellationHandler cancellationHandler) noexcept override;

    isoft::uds::Result<void> DownloadData(
        std::uint8_t blockSequenceCounter,
        std::vector<std::uint8_t> transferRequestParameterRecord,
        isoft::uds::server::MetaInfoMap& metaInfo,
        isoft::uds::server::CancellationHandler cancellationHandler) noexcept override;

    isoft::uds::Result<std::vector<std::uint8_t>> RequestDownloadExit(
        std::vector<std::uint8_t> transferRequestParameterRecord,
        isoft::uds::server::MetaInfoMap& metaInfo,
        isoft::uds::server::CancellationHandler cancellationHandler) noexcept override;

    std::string file_path() const { return file_path_; }

private:
    static isoft::uds::Result<void> Error(isoft::uds::server::NrcErrc nrc);

    std::string file_path_;
    std::ofstream output_;
    std::uint8_t expected_block_ = 0U;
};

}  // namespace diag
