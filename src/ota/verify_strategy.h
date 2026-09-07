#pragma once

#include <memory>
#include <string>

namespace ota {

class VerifyStrategy {
public:
    virtual ~VerifyStrategy() = default;

    virtual std::string digest(const std::string& path, std::string& error) const = 0;
    virtual std::string name() const = 0;

    bool verify(const std::string& path, const std::string& expected_digest, std::string& error) const;
};

std::unique_ptr<VerifyStrategy> make_verify_strategy(const std::string& algorithm);

}  // namespace ota

