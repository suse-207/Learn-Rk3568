#include "ara/log/logger.h"

#include <cstdio>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <string>

namespace ara {
namespace log {

namespace {

std::string LevelName(LogLevel const level)
{
    switch (level) {
        case LogLevel::kFatal:
            return "FATAL";
        case LogLevel::kError:
            return "ERROR";
        case LogLevel::kWarn:
            return "WARN ";
        case LogLevel::kInfo:
            return "INFO ";
        case LogLevel::kDebug:
            return "DEBUG";
        case LogLevel::kVerbose:
            return "VERB ";
        default:
            return "     ";
    }
}

std::mutex gLogMutex;
std::map< std::string, std::unique_ptr< Logger > > gLoggers;

}  // namespace

LogStream::LogStream(LogLevel const level) : level_{level}, buffer_{std::make_shared< std::ostringstream >()}
{
}

LogStream::LogStream(LogStream&& other) noexcept
    : level_{other.level_}, buffer_{std::move(other.buffer_)}
{
}

LogStream& LogStream::operator=(LogStream&& other) noexcept
{
    if (this != &other) {
        Flush();
        level_  = other.level_;
        buffer_ = std::move(other.buffer_);
    }
    return *this;
}

LogStream::~LogStream()
{
    Flush();
}

void LogStream::Flush() noexcept
{
    if (!buffer_) {
        return;
    }
    std::string message = buffer_->str();
    buffer_.reset();
    if (message.empty()) {
        return;
    }
    std::lock_guard< std::mutex > lock{gLogMutex};
    std::cout << "[" << LevelName(level_) << "] " << message << std::endl;
}

LogStream& LogStream::operator<<(LogHex8 const& value)
{
    if (buffer_) {
        char tmp[8];
        std::snprintf(tmp, sizeof(tmp), "0x%02X", value.value);
        (*buffer_) << tmp;
    }
    return *this;
}

LogStream& LogStream::operator<<(LogHex16 const& value)
{
    if (buffer_) {
        char tmp[12];
        std::snprintf(tmp, sizeof(tmp), "0x%04X", value.value);
        (*buffer_) << tmp;
    }
    return *this;
}

LogStream& LogStream::operator<<(std::vector< std::uint8_t > const& value)
{
    if (buffer_) {
        (*buffer_) << "[";
        for (std::size_t i = 0; i < value.size(); ++i) {
            char tmp[8];
            std::snprintf(tmp, sizeof(tmp), "%s%02X", i == 0 ? "" : " ", value[i]);
            (*buffer_) << tmp;
        }
        (*buffer_) << "]";
    }
    return *this;
}

Logger::Logger(std::string context, LogLevel const level)
    : context_{std::move(context)}, minLevel_{level}
{
}

LogStream Logger::LogFatal() const noexcept
{
    return LogStream{LogLevel::kFatal};
}

LogStream Logger::LogError() const noexcept
{
    return LogStream{LogLevel::kError};
}

LogStream Logger::LogWarn() const noexcept
{
    return LogStream{LogLevel::kWarn};
}

LogStream Logger::LogInfo() const noexcept
{
    return LogStream{LogLevel::kInfo};
}

LogStream Logger::LogDebug() const noexcept
{
    return LogStream{LogLevel::kDebug};
}

LogStream Logger::LogVerbose() const noexcept
{
    return LogStream{LogLevel::kVerbose};
}

bool Logger::IsEnabled(LogLevel const level) const noexcept
{
    return level <= minLevel_;
}

Logger& CreateLogger(ara::core::StringView const contextId, ara::core::StringView const description) noexcept
{
    return CreateLogger(contextId, description, LogLevel::kInfo);
}

Logger& CreateLogger(ara::core::StringView const contextId,
                     ara::core::StringView const description,
                     LogLevel const level) noexcept
{
    (void)description;
    std::string key(contextId.data(), contextId.size());
    std::lock_guard< std::mutex > lock{gLogMutex};
    auto it = gLoggers.find(key);
    if (it != gLoggers.end()) {
        return *it->second;
    }
    auto logger = std::make_unique< Logger >(key, level);
    Logger& ref = *logger;
    gLoggers.emplace(std::move(key), std::move(logger));
    return ref;
}

}  // namespace log
}  // namespace ara
