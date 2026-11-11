#pragma once

#include <string>
#include <functional>

namespace myframework {

enum class LogLevel { DEBUG = 0, INFO = 1, WARN = 2, ERROR = 3, FATAL = 4 };

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
};

// 日志宏
#define MFLOG_DEBUG(tag, fmt, ...)  /* TODO */
#define MFLOG_INFO(tag, fmt, ...)   /* TODO */
#define MFLOG_WARN(tag, fmt, ...)   /* TODO */
#define MFLOG_ERROR(tag, fmt, ...)  /* TODO */
#define MFLOG_FATAL(tag, fmt, ...)  /* TODO */

} // namespace myframework