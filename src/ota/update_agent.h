#pragma once

#include <string>

namespace ota {

class HttpDownloader;
class UcmService;

/// The Update Agent (client side): downloads the software package over HTTP,
/// wraps it into a signed package (manifest + payload), pushes it block-wise to
/// the target UCM via TransferStart/TransferData/TransferExit, then asks the
/// UCM to ProcessSwPackage.
class UpdateAgent {
public:
    UpdateAgent(HttpDownloader& downloader, UcmService& ucm);

    /// @param url package (payload) URL
    /// @param version target version
    /// @return true if the package was transferred and processing started.
    bool Update(const std::string& url, const std::string& version, std::string& error);

private:
    HttpDownloader& downloader_;
    UcmService& ucm_;
};

}  // namespace ota
