#pragma once

#include <string>
#include <functional>

namespace myframework {

enum class LogLevel { DEBUG = 0, INFO = 1, WARN = 2, ERROR = 3, FATAL = 4 };

void logFormat(LogLevel lv, const char* tag, const char* fmt, ...);

class Logger {
public:
    static Logger& instance();

    void setLevel(LogLevel level);
    LogLevel level() const;

    void log(LogLevel lv, const std::string& tag, const std::string& msg);
    void debug(const std::string& tag, const std::string& msg);
    void info(const std::string& tag, const std::string& msg);
    void warn(const std::string& tag, const std::string& msg);
    void error(const std::string& tag, const std::string& msg);
    void fatal(const std::string& tag, const std::string& msg);

private:
    Logger() = default;
    LogLevel level_ = LogLevel::DEBUG;
};

// 日志宏
#define MFLOG_DEBUG(tag, fmt, ...)  myframework::logFormat(myframework::LogLevel::DEBUG, tag, fmt, ##__VA_ARGS__)
#define MFLOG_INFO(tag, fmt, ...)   myframework::logFormat(myframework::LogLevel::INFO,  tag, fmt, ##__VA_ARGS__)
#define MFLOG_WARN(tag, fmt, ...)   myframework::logFormat(myframework::LogLevel::WARN,  tag, fmt, ##__VA_ARGS__)
#define MFLOG_ERROR(tag, fmt, ...)  myframework::logFormat(myframework::LogLevel::ERROR, tag, fmt, ##__VA_ARGS__)
#define MFLOG_FATAL(tag, fmt, ...)  myframework::logFormat(myframework::LogLevel::FATAL, tag, fmt, ##__VA_ARGS__)

} // namespace myframework