#include "doip/doip_frame.h"

#include <cstring>

namespace doip {

bool parse_header(const std::uint8_t* data, std::size_t len, Header& header) {
    if (data == nullptr || len < kHeaderSize) {
        return false;
    }
    header.version = data[0];
    header.inverse_version = data[1];
    header.payload_type = static_cast<std::uint16_t>((data[2] << 8) | data[3]);
    header.payload_length = (static_cast<std::uint32_t>(data[4]) << 24) |
                            (static_cast<std::uint32_t>(data[5]) << 16) |
                            (static_cast<std::uint32_t>(data[6]) << 8) |
                            static_cast<std::uint32_t>(data[7]);
    return true;
}

std::size_t build_frame(std::uint8_t* out, std::size_t capacity,
                        std::uint16_t payload_type,
                        const std::uint8_t* payload, std::size_t payload_len) {
    if (out == nullptr || capacity < kHeaderSize + payload_len) {
        return 0;
    }
    out[0] = kProtocolVersion;
    out[1] = static_cast<std::uint8_t>(~kProtocolVersion);
    out[2] = static_cast<std::uint8_t>(payload_type >> 8);
    out[3] = static_cast<std::uint8_t>(payload_type & 0xFF);
    out[4] = static_cast<std::uint8_t>((payload_len >> 24) & 0xFF);
    out[5] = static_cast<std::uint8_t>((payload_len >> 16) & 0xFF);
    out[6] = static_cast<std::uint8_t>((payload_len >> 8) & 0xFF);
    out[7] = static_cast<std::uint8_t>(payload_len & 0xFF);
    if (payload_len > 0 && payload != nullptr) {
        std::memcpy(out + kHeaderSize, payload, payload_len);
    }
    return kHeaderSize + payload_len;
}

}  // namespace doip
