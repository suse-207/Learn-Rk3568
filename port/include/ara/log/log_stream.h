#pragma once

#include <cstdint>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace ara {
namespace log {

enum class LogLevel : std::uint8_t
{
    kOff = 0,
    kFatal,
    kError,
    kWarn,
    kInfo,
    kDebug,
    kVerbose,
    kAll
};

struct LogHex8
{
    explicit LogHex8(std::uint8_t const value) : value{value} {}
    std::uint8_t value;
};

struct LogHex16
{
    explicit LogHex16(std::uint16_t const value) : value{value} {}
    std::uint16_t value;
};

inline LogHex8 HexFormat(std::uint8_t const& value) noexcept { return LogHex8{value}; }
inline LogHex16 HexFormat(std::uint16_t const& value) noexcept { return LogHex16{value}; }

class LogStream
{
public:
    explicit LogStream(LogLevel const level);
    ~LogStream();

    LogStream(LogStream&& other) noexcept;
    LogStream& operator=(LogStream&& other) noexcept;
    LogStream(LogStream const&) = delete;
    LogStream& operator=(LogStream const&) = delete;

    void Flush() noexcept;

    template < typename T >
    LogStream& operator<<(T const& value)
    {
        if (buffer_) {
            (*buffer_) << value;
        }
        return *this;
    }

    LogStream& operator<<(LogHex8 const& value);
    LogStream& operator<<(LogHex16 const& value);
    LogStream& operator<<(std::vector< std::uint8_t > const& value);

private:
    LogLevel level_;
    std::shared_ptr< std::ostringstream > buffer_;
};

}  // namespace log
}  // namespace ara
