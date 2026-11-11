#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#define vehicle_status vs_a
#include "../../src/vehicle_status/vehicle_status_protocol.h"
#undef vehicle_status

#define vehicle_status vs_b
#include ".gen/vehicle_status/vehicle_status_protocol.h"
#undef vehicle_status

static int failures = 0;

static void expect(const char* what, const std::vector<std::uint8_t>& a,
                   const std::vector<std::uint8_t>& b) {
    if (a == b) {
        std::printf("OK   %-30s %zu bytes\n", what, a.size());
        return;
    }
    ++failures;
    std::printf("FAIL %-30s legacy=%zu gen=%zu\n  legacy:", what, a.size(), b.size());
    for (std::size_t i = 0; i < a.size(); ++i) std::printf(" %02x", a[i]);
    std::printf("\n  gen   :");
    for (std::size_t i = 0; i < b.size(); ++i) std::printf(" %02x", b[i]);
    std::printf("\n");
}

int main() {
    {
        std::vector<std::uint8_t> a;
        vs_a::AppendString(a, "1.2.3");
        vs_a::AppendU64(a, 1234567890123ULL);
        vs_a::AppendString(a, "deadbeef");
        std::vector<std::uint8_t> b;
        vs_b::EncodeStartUpdateRequest(b, "1.2.3", 1234567890123ULL, "deadbeef");
        expect("StartUpdate.request", a, b);
    }
    {
        std::vector<std::uint8_t> a;
        vs_a::AppendU32(a, 0);
        vs_a::AppendString(a, "1.0.0");
        vs_a::AppendString(a, "_a");
        a.push_back(static_cast<std::uint8_t>(6));
        std::vector<std::uint8_t> b;
        vs_b::EncodeGetStatusResponse(b, vs_b::Errc::kSuccess, "1.0.0", "_a",
                                      vs_b::OtaState::kSuccess);
        expect("GetStatus.response(success)", a, b);
    }
    {
        std::vector<std::uint8_t> a;
        vs_a::AppendU32(a, 3);
        std::vector<std::uint8_t> b;
        vs_b::EncodeGetStatusResponse(b, vs_b::Errc::kBusy, "", "", vs_b::OtaState::kIdle);
        expect("GetStatus.response(busy)", a, b);
    }
    {
        std::vector<std::uint8_t> a;
        vs_a::AppendU32(a, 0);
        a.push_back(static_cast<std::uint8_t>(3));
        vs_a::AppendU32(a, 75);
        vs_a::AppendString(a, "writing inactive slot");
        std::vector<std::uint8_t> b;
        vs_b::EncodeGetProgressResponse(b, vs_b::Errc::kSuccess, vs_b::OtaState::kInstalling, 75,
                                        "writing inactive slot");
        expect("GetProgress.response", a, b);
    }
    {
        std::vector<std::uint8_t> a;
        vs_a::AppendOtaState(a, vs_a::OtaState::kDownloading, 15, "copying");
        std::vector<std::uint8_t> b;
        vs_b::EncodeOtaStateChangedEvent(b, vs_b::OtaState::kDownloading, 15, "copying");
        expect("OtaStateChanged.event", a, b);
    }
    {
        std::vector<std::uint8_t> wire;
        vs_b::EncodeOtaStateChangedEvent(wire, vs_b::OtaState::kInstalling, 75, "writing");
        std::size_t pos = 0;
        vs_b::OtaState state = vs_b::OtaState::kIdle;
        std::uint32_t percent = 0;
        std::string detail;
        const bool ok = vs_b::DecodeOtaStateChangedEvent(wire, pos, state, percent, detail);
        const bool same = ok && state == vs_b::OtaState::kInstalling && percent == 75 &&
                          detail == "writing" && pos == wire.size();
        std::printf("%s %-30s pos=%zu/%zu\n", same ? "OK  " : "FAIL",
                    "OtaStateChanged.decode", pos, wire.size());
        if (!same) ++failures;
    }
    {
        std::vector<std::uint8_t> wire;
        vs_b::EncodeStartUpdateRequest(wire, "9.9.9", 42, "abc");
        std::size_t pos = 0;
        std::string version;
        std::string sha;
        std::uint64_t size = 0;
        const bool ok = vs_b::DecodeStartUpdateRequest(wire, pos, version, size, sha);
        const bool same = ok && version == "9.9.9" && size == 42 && sha == "abc" &&
                          pos == wire.size();
        std::printf("%s %-30s pos=%zu/%zu\n", same ? "OK  " : "FAIL",
                    "StartUpdate.decode", pos, wire.size());
        if (!same) ++failures;
    }
    std::printf("\n%s (failures=%d)\n", failures == 0 ? "ALL PASS" : "HAS FAILURES", failures);
    return failures == 0 ? 0 : 1;
}
