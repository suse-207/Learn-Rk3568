#include "ota/verify_strategy.h"

#include <openssl/sha.h>

#include <array>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <sstream>

namespace ota {
namespace {

std::string bytes_to_hex(const unsigned char* data, std::size_t size) {
    static const char* digits = "0123456789abcdef";
    std::string out;
    out.reserve(size * 2);
    for (std::size_t i = 0; i < size; ++i) {
        out.push_back(digits[data[i] >> 4]);
        out.push_back(digits[data[i] & 0x0F]);
    }
    return out;
}

class Sha256VerifyStrategy final : public VerifyStrategy {
public:
    std::string digest(const std::string& path, std::string& error) const override {
        std::ifstream input(path, std::ios::binary);
        if (!input) {
            error = "cannot open file";
            return {};
        }

        SHA256_CTX context;
        SHA256_Init(&context);

        std::array<char, 4096> buffer{};
        while (input) {
            input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
            const std::streamsize count = input.gcount();
            if (count > 0) {
                SHA256_Update(&context, buffer.data(), static_cast<std::size_t>(count));
            }
        }

        std::array<unsigned char, SHA256_DIGEST_LENGTH> hash{};
        SHA256_Final(hash.data(), &context);
        return bytes_to_hex(hash.data(), hash.size());
    }

    std::string name() const override { return "sha256"; }
};

class Crc32VerifyStrategy final : public VerifyStrategy {
public:
    std::string digest(const std::string& path, std::string& error) const override {
        std::ifstream input(path, std::ios::binary);
        if (!input) {
            error = "cannot open file";
            return {};
        }

        std::uint32_t crc = 0xFFFFFFFFU;
        std::array<char, 4096> buffer{};
        while (input) {
            input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
            const std::streamsize count = input.gcount();
            for (std::streamsize i = 0; i < count; ++i) {
                const unsigned char byte = static_cast<unsigned char>(buffer[static_cast<std::size_t>(i)]);
                crc = table()[(crc ^ byte) & 0xFFU] ^ (crc >> 8);
            }
        }
        crc ^= 0xFFFFFFFFU;

        const unsigned char bytes[4] = {
            static_cast<unsigned char>((crc >> 24) & 0xFF),
            static_cast<unsigned char>((crc >> 16) & 0xFF),
            static_cast<unsigned char>((crc >> 8) & 0xFF),
            static_cast<unsigned char>(crc & 0xFF),
        };
        return bytes_to_hex(bytes, sizeof(bytes));
    }

    std::string name() const override { return "crc32"; }

private:
    static const std::array<std::uint32_t, 256>& table() {
        static const std::array<std::uint32_t, 256> table = []() {
            std::array<std::uint32_t, 256> result{};
            for (std::uint32_t i = 0; i < 256; ++i) {
                std::uint32_t c = i;
                for (int k = 0; k < 8; ++k) {
                    c = (c & 1U) ? (0xEDB88320U ^ (c >> 1)) : (c >> 1);
                }
                result[i] = c;
            }
            return result;
        }();
        return table;
    }
};

}  // namespace

bool VerifyStrategy::verify(const std::string& path, const std::string& expected_digest,
                            std::string& error) const {
    const std::string actual = digest(path, error);
    if (!error.empty()) {
        return false;
    }
    if (expected_digest.empty()) {
        return true;
    }

    const auto normalized = [](std::string value) {
        for (char& c : value) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        return value;
    };

    if (normalized(actual) != normalized(expected_digest)) {
        error = "digest mismatch";
        return false;
    }
    return true;
}

std::unique_ptr<VerifyStrategy> make_verify_strategy(const std::string& algorithm) {
    const auto normalized = [](std::string value) {
        for (char& c : value) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        return value;
    };

    const std::string name = normalized(algorithm);
    if (name == "sha256" || name == "sha-256") {
        return std::make_unique<Sha256VerifyStrategy>();
    }
    if (name == "crc32" || name == "crc-32") {
        return std::make_unique<Crc32VerifyStrategy>();
    }
    return nullptr;
}

}  // namespace ota
