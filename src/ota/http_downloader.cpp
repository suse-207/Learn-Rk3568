#include "ota/http_downloader.h"

#include <curl/curl.h>

#include <fstream>

namespace ota {

namespace {

std::size_t WriteCallback(void* ptr, std::size_t size, std::size_t nmemb, void* userdata) {
    auto* out = static_cast<std::ofstream*>(userdata);
    out->write(static_cast<const char*>(ptr), static_cast<std::streamsize>(size * nmemb));
    return size * nmemb;
}

}  // namespace

bool HttpDownloader::Download(const std::string& url, const std::string& out_path, std::string& error) {
    CURL* curl = curl_easy_init();
    if (curl == nullptr) {
        error = "curl_easy_init failed";
        return false;
    }

    std::ofstream out(out_path, std::ios::binary | std::ios::trunc);
    if (!out) {
        error = "open output file failed: " + out_path;
        curl_easy_cleanup(curl);
        return false;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &out);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    // Demo only: skip TLS verification; use a proper CA bundle in production.
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

    const CURLcode rc = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    out.flush();

    if (rc != CURLE_OK) {
        error = curl_easy_strerror(rc);
        return false;
    }
    if (out.bad()) {
        error = "write to output file failed";
        return false;
    }
    return true;
}

}  // namespace ota
