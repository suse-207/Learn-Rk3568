#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace ota {

/// Software package manifest, modeled after ara::ucm's package manifest.
struct PackageManifest {
    std::string name;
    std::string version;
    std::vector<std::string> dependencies;
    std::string signature;  // HMAC-SHA256 hex over the payload.
};

/// HMAC-SHA256 hex of `data` using a demo key. Implemented in package_manifest.cpp
/// (uses OpenSSL). Returns empty string on failure.
std::string HmacSha256Hex(const std::vector<std::uint8_t>& data);

// ---- Minimal binary helpers (namespace-local to avoid coupling to ucm) ----

inline void ManifestAppendU32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    const std::uint8_t* p = reinterpret_cast<const std::uint8_t*>(&v);
    out.insert(out.end(), p, p + sizeof(v));
}

inline void ManifestAppendString(std::vector<std::uint8_t>& out, const std::string& s) {
    ManifestAppendU32(out, static_cast<std::uint32_t>(s.size()));
    out.insert(out.end(), s.begin(), s.end());
}

inline bool ManifestReadU32(const std::vector<std::uint8_t>& in, std::size_t& pos, std::uint32_t& v) {
    if (pos + sizeof(v) > in.size()) {
        return false;
    }
    std::memcpy(&v, in.data() + pos, sizeof(v));
    pos += sizeof(v);
    return true;
}

inline bool ManifestReadString(const std::vector<std::uint8_t>& in, std::size_t& pos, std::string& s) {
    std::uint32_t len = 0;
    if (!ManifestReadU32(in, pos, len)) {
        return false;
    }
    if (pos + len > in.size()) {
        return false;
    }
    s.assign(reinterpret_cast<const char*>(in.data() + pos), len);
    pos += len;
    return true;
}

/// Build the transfer package = [u32(manifest_blob_len)][manifest_blob][payload].
inline std::vector<std::uint8_t> BuildPackage(const PackageManifest& m,
                                              const std::vector<std::uint8_t>& payload) {
    std::vector<std::uint8_t> blob;
    ManifestAppendString(blob, m.name);
    ManifestAppendString(blob, m.version);
    ManifestAppendU32(blob, static_cast<std::uint32_t>(m.dependencies.size()));
    for (const auto& d : m.dependencies) {
        ManifestAppendString(blob, d);
    }
    ManifestAppendString(blob, m.signature);

    std::vector<std::uint8_t> pkg;
    ManifestAppendU32(pkg, static_cast<std::uint32_t>(blob.size()));
    pkg.insert(pkg.end(), blob.begin(), blob.end());
    pkg.insert(pkg.end(), payload.begin(), payload.end());
    return pkg;
}

/// Unpack the transfer package. Returns false if the manifest is malformed.
inline bool UnpackPackage(const std::vector<std::uint8_t>& pkg,
                          PackageManifest& m,
                          std::vector<std::uint8_t>& payload) {
    std::size_t pos = 0;
    std::uint32_t blob_len = 0;
    if (!ManifestReadU32(pkg, pos, blob_len) || pos + blob_len > pkg.size()) {
        return false;
    }
    const std::vector<std::uint8_t> blob(pkg.begin() + pos, pkg.begin() + pos + blob_len);
    pos += blob_len;
    payload.assign(pkg.begin() + pos, pkg.end());

    std::size_t bpos = 0;
    if (!ManifestReadString(blob, bpos, m.name) ||
        !ManifestReadString(blob, bpos, m.version)) {
        return false;
    }
    std::uint32_t dep_count = 0;
    if (!ManifestReadU32(blob, bpos, dep_count)) {
        return false;
    }
    m.dependencies.clear();
    for (std::uint32_t i = 0; i < dep_count; ++i) {
        std::string d;
        if (!ManifestReadString(blob, bpos, d)) {
            return false;
        }
        m.dependencies.push_back(std::move(d));
    }
    if (!ManifestReadString(blob, bpos, m.signature)) {
        return false;
    }
    return true;
}

}  // namespace ota
