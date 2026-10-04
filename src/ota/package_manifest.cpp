#include "ota/package_manifest.h"

#include <openssl/evp.h>
#include <openssl/hmac.h>

namespace ota {

std::string HmacSha256Hex(const std::vector<std::uint8_t>& data) {
    const std::string key = "demo-ota-secret";  // Demo key; real UCM uses a certificate chain.
    unsigned char out[EVP_MAX_MD_SIZE];
    unsigned int out_len = 0;
    if (HMAC(EVP_sha256(), key.data(), static_cast<int>(key.size()),
             data.data(), data.size(), out, &out_len) == nullptr) {
        return std::string();
    }

    static const char kHex[] = "0123456789abcdef";
    std::string hex;
    hex.reserve(out_len * 2);
    for (unsigned int i = 0; i < out_len; ++i) {
        hex += kHex[(out[i] >> 4) & 0xF];
        hex += kHex[out[i] & 0xF];
    }
    return hex;
}

}  // namespace ota
