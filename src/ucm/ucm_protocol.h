#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace ucm {

// SOME-IP method IDs for the UCM PackageManagement service (demo assignment).
constexpr std::uint16_t kMethodTransferStart = 0x0001;
constexpr std::uint16_t kMethodTransferData = 0x0002;
constexpr std::uint16_t kMethodTransferExit = 0x0003;
constexpr std::uint16_t kMethodProcessSwPackage = 0x0004;
constexpr std::uint16_t kMethodDeleteTransfer = 0x0005;
constexpr std::uint16_t kMethodActivate = 0x0006;
constexpr std::uint16_t kMethodFinish = 0x0007;
constexpr std::uint16_t kMethodCancel = 0x0008;
constexpr std::uint16_t kMethodRollback = 0x0009;
constexpr std::uint16_t kMethodRevertProcessedSwPackages = 0x000A;
constexpr std::uint16_t kMethodGetSwPackages = 0x000B;
constexpr std::uint16_t kMethodGetSwClusterInfo = 0x000C;
constexpr std::uint16_t kMethodGetSwClusterChangeInfo = 0x000D;
constexpr std::uint16_t kMethodGetSwClusterDescription = 0x000E;
constexpr std::uint16_t kMethodGetSwProcessProgress = 0x000F;
constexpr std::uint16_t kMethodGetHistory = 0x0010;
constexpr std::uint16_t kMethodGetId = 0x0011;

// ---- Append helpers (little-endian native; same machine on both sides) ----

inline void AppendU64(std::vector<std::uint8_t>& out, std::uint64_t v) {
    const std::uint8_t* p = reinterpret_cast<const std::uint8_t*>(&v);
    out.insert(out.end(), p, p + sizeof(v));
}

inline void AppendU32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    const std::uint8_t* p = reinterpret_cast<const std::uint8_t*>(&v);
    out.insert(out.end(), p, p + sizeof(v));
}

inline void AppendString(std::vector<std::uint8_t>& out, const std::string& s) {
    AppendU32(out, static_cast<std::uint32_t>(s.size()));
    out.insert(out.end(), s.begin(), s.end());
}

inline void AppendBytes(std::vector<std::uint8_t>& out, const std::vector<std::uint8_t>& b) {
    AppendU32(out, static_cast<std::uint32_t>(b.size()));
    out.insert(out.end(), b.begin(), b.end());
}

// ---- Read helpers ----

inline bool ReadU64(const std::vector<std::uint8_t>& in, std::size_t& pos, std::uint64_t& v) {
    if (pos + sizeof(v) > in.size()) {
        return false;
    }
    std::memcpy(&v, in.data() + pos, sizeof(v));
    pos += sizeof(v);
    return true;
}

inline bool ReadU32(const std::vector<std::uint8_t>& in, std::size_t& pos, std::uint32_t& v) {
    if (pos + sizeof(v) > in.size()) {
        return false;
    }
    std::memcpy(&v, in.data() + pos, sizeof(v));
    pos += sizeof(v);
    return true;
}

inline bool ReadString(const std::vector<std::uint8_t>& in, std::size_t& pos, std::string& s) {
    std::uint32_t len = 0;
    if (!ReadU32(in, pos, len)) {
        return false;
    }
    if (pos + len > in.size()) {
        return false;
    }
    s.assign(reinterpret_cast<const char*>(in.data() + pos), len);
    pos += len;
    return true;
}

inline bool ReadBytes(const std::vector<std::uint8_t>& in, std::size_t& pos, std::vector<std::uint8_t>& b) {
    std::uint32_t len = 0;
    if (!ReadU32(in, pos, len)) {
        return false;
    }
    if (pos + len > in.size()) {
        return false;
    }
    b.assign(in.begin() + pos, in.begin() + pos + len);
    pos += len;
    return true;
}

}  // namespace ucm
