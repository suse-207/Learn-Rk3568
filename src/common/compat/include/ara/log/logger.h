#pragma once

#include <string>

#include "ara/core/string_view.h"
#include "ara/log/log_stream.h"

namespace ara {
namespace log {

class Logger
{
public:
    Logger(std::string context, LogLevel level = LogLevel::kWarn);

    LogStream LogFatal() const noexcept;
    LogStream LogError() const noexcept;
    LogStream LogWarn() const noexcept;
    LogStream LogInfo() const noexcept;
    LogStream LogDebug() const noexcept;
    LogStream LogVerbose() const noexcept;

    bool IsEnabled(LogLevel const level) const noexcept;

private:
    std::string context_;
    LogLevel minLevel_;
};

Logger& CreateLogger(ara::core::StringView const contextId, ara::core::StringView const description) noexcept;
Logger& CreateLogger(ara::core::StringView const contextId,
                     ara::core::StringView const description,
                     LogLevel const level) noexcept;

}  // namespace log
}  // namespace ara
