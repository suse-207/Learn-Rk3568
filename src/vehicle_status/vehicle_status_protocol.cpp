#include "vehicle_status/vehicle_status_protocol.h"

namespace vehicle_status {

void AppendU16(std::vector<std::uint8_t>& out, std::uint16_t v) {
    out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFFU));
    out.push_back(static_cast<std::uint8_t>(v & 0xFFU));
}

void AppendU32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    out.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFFU));
    out.push_back(static_cast<std::uint8_t>(v & 0xFFU));
}

void AppendU64(std::vector<std::uint8_t>& out, std::uint64_t v) {
    for (int shift = 56; shift >= 0; shift -= 8) {
        out.push_back(static_cast<std::uint8_t>((v >> shift) & 0xFFU));
    }
}

void AppendString(std::vector<std::uint8_t>& out, const std::string& s) {
    AppendU32(out, static_cast<std::uint32_t>(s.size()));
    out.insert(out.end(), s.begin(), s.end());
}

bool ReadU16(const std::vector<std::uint8_t>& in, std::size_t& pos, std::uint16_t& v) {
    if (pos + 2 > in.size()) {
        return false;
    }
    v = static_cast<std::uint16_t>((in[pos] << 8) | in[pos + 1]);
    pos += 2;
    return true;
}

bool ReadU32(const std::vector<std::uint8_t>& in, std::size_t& pos, std::uint32_t& v) {
    if (pos + 4 > in.size()) {
        return false;
    }
    v = (static_cast<std::uint32_t>(in[pos]) << 24) |
        (static_cast<std::uint32_t>(in[pos + 1]) << 16) |
        (static_cast<std::uint32_t>(in[pos + 2]) << 8) |
        static_cast<std::uint32_t>(in[pos + 3]);
    pos += 4;
    return true;
}

bool ReadU64(const std::vector<std::uint8_t>& in, std::size_t& pos, std::uint64_t& v) {
    if (pos + 8 > in.size()) {
        return false;
    }
    v = 0;
    for (int i = 0; i < 8; ++i) {
        v = (v << 8) | in[pos + i];
    }
    pos += 8;
    return true;
}

bool ReadString(const std::vector<std::uint8_t>& in, std::size_t& pos, std::string& s) {
    std::uint32_t len = 0;
    if (!ReadU32(in, pos, len) || pos + len > in.size()) {
        return false;
    }
    s.assign(in.begin() + pos, in.begin() + pos + len);
    pos += len;
    return true;
}

}  // namespace vehicle_status
