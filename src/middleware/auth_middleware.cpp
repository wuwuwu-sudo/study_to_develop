#include "middleware/auth_middleware.h"

#include <algorithm>
#include <exception>
#include <string>

#include "infrastructure/common/logger.h"

namespace presentation::middleware {

using infrastructure::common::Logger;

namespace {

// 从请求中提取会话 ID：优先名为 cookie_name 的请求头（前端可显式携带），
// 其次从 Cookie 头中解析（浏览器 HttpOnly cookie 自动携带）。
// 与 presentation::handlers 层 read_session_id 的语义保持一致，
// 避免“API 可过、受保护页面却 401”的会话丢失不一致。
std::string session_from_request(const presentation::http::HttpRequest& request,
                                 const std::string& cookie_name) {
    // 1) 请求头优先
    const std::string sid = request.header(cookie_name);
    if (!sid.empty()) {
        return sid;
    }

    // 2) 从 Cookie 头解析（防御：名称不匹配或缺失时返回空串）
    const std::string cookie_header = request.header("Cookie");
    const std::string key = cookie_name + "=";
    const std::size_t pos = cookie_header.find(key);
    if (pos == std::string::npos) {
        return "";
    }
    const std::size_t start = pos + key.size();
    const std::size_t end = cookie_header.find(';', start);
    if (end == std::string::npos) {
        return cookie_header.substr(start);
    }
    return cookie_header.substr(start, end - start);
}

}  // namespace

AuthMiddleware::AuthMiddleware(infrastructure::session::SessionManager& session_manager)
    : session_manager_(session_manager) {}

AuthMiddleware& AuthMiddleware::cookie_name(const std::string& name) {
    // 防御性编程：空 cookie 名无意义，忽略以免误用空键查找会话
    if (!name.empty()) {
        cookie_name_ = name;
    }
    return *this;
}

AuthMiddleware& AuthMiddleware::protect(const std::string& path) {
    // 防御性编程：忽略空路径与重复注册，避免空路径误拦截所有请求
    if (!path.empty() &&
        std::find(protected_paths_.begin(), protected_paths_.end(), path) ==
            protected_paths_.end()) {
        protected_paths_.push_back(path);
    }
    return *this;
}

AuthMiddleware& AuthMiddleware::public_path(const std::string& path) {
    if (!path.empty() &&
        std::find(public_paths_.begin(), public_paths_.end(), path) ==
            public_paths_.end()) {
        public_paths_.push_back(path);
    }
    return *this;
}

void AuthMiddleware::handle(const presentation::http::HttpRequest& request,
                            presentation::http::HttpResponse& response,
                            Next next) {
    // 防御性：cookie 名异常为空时回退到默认值
    const std::string cookie = cookie_name_.empty() ? "session_id" : cookie_name_;
    const std::string& path = request.path;

    const bool is_public =
        std::find(public_paths_.begin(), public_paths_.end(), path) != public_paths_.end();
    const bool is_protected =
        std::find(protected_paths_.begin(), protected_paths_.end(), path) !=
        protected_paths_.end();

    // 公开路径或未列入保护范围：直接放行
    if (is_public || !is_protected) {
        next();
        return;
    }

    const std::string session_id = session_from_request(request, cookie);
    if (session_id.empty()) {
        Logger::instance().warn("Auth: missing session for protected path " + path);
        response.set_status(401);
        response.set_text("Unauthorized");
        return;
    }

    // 防御性编程：底层会话存储异常时按未认证处理，避免进程崩溃
    try {
        if (!session_manager_.validate_session(session_id)) {
            Logger::instance().warn("Auth: invalid or expired session for path " + path);
            response.set_status(401);
            response.set_text("Unauthorized");
            return;
        }
    } catch (const std::exception& e) {
        Logger::instance().error(std::string("Auth: session validation threw: ") + e.what());
        response.set_status(401);
        response.set_text("Unauthorized");
        return;
    }

    next();
}

}  // namespace presentation::middleware
