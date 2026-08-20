#include "log.h"
#include <iostream>
#include <chrono>
#include <iomanip>
#include <sstream>

Logger::Logger() = default;
Logger::~Logger() {
    if (ofs.is_open()) ofs.close();
}

Logger& Logger::instance() {
    static Logger inst;
    return inst;
}

void Logger::set_file(const std::string& filename) {
    file_enabled = true;
    ofs.open(filename, std::ios::app); // 追加模式
    if (!ofs.is_open()) {
        std::cerr << "[ERROR] Logger::set_file() failed to open: " << filename << std::endl;
        file_enabled = false;
    } else {
        std::cout << "[INFO] Logger::set_file() opened: " << filename << std::endl;
    }
}

std::string Logger::timestamp() {
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    struct tm tm_now;
    localtime_r(&t, &tm_now);

    std::ostringstream oss;
    oss << std::put_time(&tm_now, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

void Logger::debug(const char* file, int line, const std::string& msg) {
    std::string line_msg = "[" + timestamp() + "] [DEBUG] " + file + ":" + std::to_string(line) + " " + msg + "\n";
    std::cerr << line_msg;
    if (file_enabled) ofs << line_msg;
}

void Logger::info(const std::string& msg) {
    std::string line_msg = "[" + timestamp() + "] [INFO] " + msg + "\n";
    std::cerr << line_msg;
    if (file_enabled) ofs << line_msg;
}

void Logger::warn(const std::string& msg) {
    std::string line_msg = "[" + timestamp() + "] [WARN] " + msg + "\n";
    std::cerr << line_msg;
    if (file_enabled) ofs << line_msg;
}

void Logger::error(const std::string& msg) {
    std::string line_msg = "[" + timestamp() + "] [ERROR] " + msg + "\n";
    std::cerr << line_msg;
    if (file_enabled) ofs << line_msg;
}