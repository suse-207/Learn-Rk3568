#include "common/log/log.h"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <mutex>

namespace {

std::mutex g_log_mutex;

const char* level_name(LogLevel level) {
    switch (level) {
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO:  return "INFO";
        case LogLevel::WARN:  return "WARN";
        case LogLevel::ERROR: return "ERROR";
    }
    return "?";
}

std::string timestamp() {
    using namespace std::chrono;
    auto now = system_clock::now();
    auto sec = system_clock::to_time_t(now);
    auto ms = duration_cast<milliseconds>(now.time_since_epoch()).count() % 1000;

    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", std::localtime(&sec));
    char out[40];
    std::snprintf(out, sizeof(out), "%s.%03d", buf, static_cast<int>(ms));
    return out;
}

}  // namespace

Logger& Logger::instance() {
    static Logger logger;
    return logger;
}

void Logger::set_level(LogLevel level) {
    level_ = level;
}

void Logger::log(LogLevel level, const char* fmt, ...) {
    if (level < level_) {
        return;
    }

    std::lock_guard<std::mutex> lock(g_log_mutex);
    std::FILE* out = (level >= LogLevel::WARN) ? stderr : stdout;

    std::fprintf(out, "[%s][%s] ", timestamp().c_str(), level_name(level));

    va_list ap;
    va_start(ap, fmt);
    std::vfprintf(out, fmt, ap);
    va_end(ap);

    std::fputc('\n', out);
    std::fflush(out);
}
