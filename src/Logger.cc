#include "myframework/Logger.h"
#include <cstdio>
#include <cstdarg>
#include <mutex>
#include <ctime>
#include <string>

namespace myframework {

Logger& Logger::instance() {
    static Logger logger;
    return logger;
}

void Logger::setLevel(LogLevel level) {
    level_ = level;
}

LogLevel Logger::level() const {
    return level_;
}

static const char* levelString(LogLevel lv) {
    switch (lv) {
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO:  return "INFO";
        case LogLevel::WARN:  return "WARN";
        case LogLevel::ERROR: return "ERROR";
        case LogLevel::FATAL: return "FATAL";
    }
    return "???";
}

void Logger::log(LogLevel lv, const std::string& tag, const std::string& msg) {
    if (lv < level_) return;

    // 获取时间戳
    time_t now = time(nullptr);
    struct tm tm_buf;
    localtime_r(&now, &tm_buf);
    char timeStr[32];
    strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &tm_buf);

    // 输出到 stderr（所有级别）
    fprintf(stderr, "[%s] [%s] [%s] %s\n",
            timeStr, levelString(lv), tag.c_str(), msg.c_str());
    fflush(stderr);

    if (lv == LogLevel::FATAL) {
        abort();
    }
}

void Logger::debug(const std::string& tag, const std::string& msg) { log(LogLevel::DEBUG, tag, msg); }
void Logger::info(const std::string& tag, const std::string& msg)  { log(LogLevel::INFO,  tag, msg); }
void Logger::warn(const std::string& tag, const std::string& msg)  { log(LogLevel::WARN,  tag, msg); }
void Logger::error(const std::string& tag, const std::string& msg) { log(LogLevel::ERROR, tag, msg); }
void Logger::fatal(const std::string& tag, const std::string& msg) { log(LogLevel::FATAL, tag, msg); }

void logFormat(LogLevel lv, const char* tag, const char* fmt, ...) {
    char buf[4096];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    Logger::instance().log(lv, tag, buf);
}

} // namespace myframework