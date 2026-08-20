#pragma once

#include <string>

namespace infrastructure::common {

enum class LogLevel {
    DEBUG,
    INFO,
    WARN,
    ERROR
};

class Logger {
public:
    static Logger& instance();

    // level: 最低记录级别（低于该级别的日志丢弃）
    // sampling: INFO 级抽样分母（1=全量；N=每 N 条 INFO 记 1 条；WARN/ERROR 永不抽样）
    void initialize(LogLevel level, const std::string& filename, bool console_output,
                    int sampling = 1);
    void shutdown();
    void set_file(const std::string& filename);
    // 运行时调整抽样分母（1=全量，N=每 N 条 INFO 记 1 条；线程安全）
    void set_sampling(int sampling);
    void set_level(LogLevel level);
    void debug(const char* file, int line, const std::string& message);
    void info(const std::string& message);
    void warn(const std::string& message);
    void error(const std::string& message);

private:
    Logger();
    ~Logger();
};

}  // namespace infrastructure::common

#define LOG_DEBUG(msg) infrastructure::common::Logger::instance().debug(__FILE__, __LINE__, msg)
#define LOG_INFO(msg)  infrastructure::common::Logger::instance().info(msg)
#define LOG_WARN(msg)  infrastructure::common::Logger::instance().warn(msg)
#define LOG_ERROR(msg) infrastructure::common::Logger::instance().error(msg)
