#pragma once
#include <string>
#include <fstream>
#include <iostream>
#include <chrono>
#include <iomanip>
#include <sstream>

#define LOG_DEBUG(msg) Logger::instance().debug(__FILE__, __LINE__, msg)
#define LOG_INFO(msg)  Logger::instance().info(msg)
#define LOG_WARN(msg)  Logger::instance().warn(msg)
#define LOG_ERROR(msg) Logger::instance().error(msg)

class Logger {
public:
    static Logger& instance();

    void set_file(const std::string& filename);
    void debug(const char* file, int line, const std::string& msg);
    void info(const std::string& msg);
    void warn(const std::string& msg);
    void error(const std::string& msg);

private:
    Logger();
    ~Logger();

    std::ofstream ofs;
    bool file_enabled = false;

    std::string timestamp();
};