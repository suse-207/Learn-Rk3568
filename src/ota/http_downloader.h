#pragma once

#include <string>

namespace ota {

/// Downloads a URL to a local file using libcurl (HTTP/HTTPS).
/// This is the "cloud -> Update Agent" hop of the OTA chain.
class HttpDownloader {
public:
    /// @return true on success; false + `error` on failure.
    bool Download(const std::string& url, const std::string& out_path, std::string& error);
};

}  // namespace ota
