#pragma once

#include <cstdarg>

enum class LogLevel {
    DEBUG = 0,
    INFO = 1,
    WARN = 2,
    ERROR = 3,
};

class Logger {
public:
    static Logger& instance();

    void set_level(LogLevel level);
    void log(LogLevel level, const char* fmt, ...);

private:
    Logger() = default;
    LogLevel level_ = LogLevel::INFO;
};

#define LOG_DEBUG(...) ::Logger::instance().log(::LogLevel::DEBUG, __VA_ARGS__)
#define LOG_INFO(...)  ::Logger::instance().log(::LogLevel::INFO,  __VA_ARGS__)
#define LOG_WARN(...)  ::Logger::instance().log(::LogLevel::WARN,  __VA_ARGS__)
#define LOG_ERROR(...) ::Logger::instance().log(::LogLevel::ERROR, __VA_ARGS__)
