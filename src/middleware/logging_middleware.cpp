#include "middleware/logging_middleware.h"

#include <chrono>
#include <string>

#include "infrastructure/common/logger.h"

namespace presentation::middleware {

using infrastructure::common::Logger;

namespace {

// 日志中记录请求体的最大长度（防御：避免超长 body 撑爆日志）
constexpr std::size_t kMaxLoggedBody = 256;

std::string method_name(HttpMethod method) {
    switch (method) {
        case HttpMethod::GET:     return "GET";
        case HttpMethod::POST:    return "POST";
        case HttpMethod::PUT:     return "PUT";
        case HttpMethod::DELETE:  return "DELETE";
        case HttpMethod::HEAD:    return "HEAD";
        case HttpMethod::OPTIONS: return "OPTIONS";
        case HttpMethod::PATCH:   return "PATCH";
    }
    return "UNKNOWN";
}

// 清洗请求体：剔除换行/回车（防日志注入）并截断到安全长度
std::string sanitize_body(const std::string& body) {
    std::string out;
    out.reserve(body.size() < kMaxLoggedBody ? body.size() : kMaxLoggedBody);
    for (const char c : body) {
        if (c == '\n' || c == '\r') {
            continue;
        }
        if (out.size() >= kMaxLoggedBody) {
            break;
        }
        out.push_back(c);
    }
    return out;
}

}  // namespace

LoggingMiddleware::LoggingMiddleware(bool log_body)
    : log_body_(log_body) {}

void LoggingMiddleware::handle(const presentation::http::HttpRequest& request,
                               presentation::http::HttpResponse& response,
                               Next next) {
    const auto start = std::chrono::steady_clock::now();

    std::string message = method_name(request.method) + " " + request.path;
    if (log_body_ && !request.body.empty()) {
        message += " body=" + sanitize_body(request.body);
    }

    next();

    const long elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - start)
                                .count();
    Logger::instance().info(message + " -> " + std::to_string(response.status_code) +
                            " (" + std::to_string(elapsed_ms) + "ms)");
}

}  // namespace presentation::middleware
