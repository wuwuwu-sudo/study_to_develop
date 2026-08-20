#include "presentation/handlers/auth_handler.h"

#include <utility>

#include <nlohmann/json.hpp>

#include "infrastructure/common/exception.h"
#include "infrastructure/common/logger.h"
#include "presentation/handlers/overload_page.h"

namespace presentation::handlers {

namespace {

using infrastructure::common::AppException;
using infrastructure::common::Logger;
using nlohmann::json;

// 解析请求体为 JSON，失败时返回空对象
json parse_body(const std::string& body) {
    try {
        return json::parse(body);
    } catch (...) {
        return json::object();
    }
}

std::string json_get_string(const json& j, const char* key, const std::string& default_value) {
    if (j.contains(key) && j[key].is_string()) {
        return j[key].get<std::string>();
    }
    return default_value;
}

int json_get_int(const json& j, const char* key, int default_value) {
    if (j.contains(key) && j[key].is_number_integer()) {
        return j[key].get<int>();
    }
    return default_value;
}

void write_json(presentation::http::HttpResponse& response, const json& j) {
    response.set_status(200);
    response.set_json(j.dump());
}

void write_error(presentation::http::HttpResponse& response, int status,
                 const std::string& message) {
    response.set_status(status);
    response.set_json(json{{"success", false}, {"message", message}}.dump());
}

// 将认证服务 AppException 映射为 HTTP 状态码
int status_for_auth_error(const AppException& e) {
    const std::string msg = e.what();
    if (msg.find("已存在") != std::string::npos) {
        return 409;  // 用户名/店铺名冲突
    }
    if (msg.find("不存在") != std::string::npos) {
        return 404;
    }
    if (msg.find("不能为空") != std::string::npos ||
        msg.find("不能少于") != std::string::npos ||
        msg.find("不能超过") != std::string::npos ||
        msg.find("无效") != std::string::npos) {
        return 400;
    }
    if (msg.find("错误") != std::string::npos ||
        msg.find("禁用") != std::string::npos) {
        return 401;
    }
    return 500;
}

// 读取会话 ID：优先取 session_id 请求头，其次解析 Cookie 头（浏览器 credentials:include 回传）
std::string read_session_id(const presentation::http::HttpRequest& request) {
    std::string sid = request.header("session_id");
    if (!sid.empty()) {
        return sid;
    }
    const std::string cookie = request.header("Cookie");
    const std::string key = "session_id=";
    std::size_t pos = cookie.find(key);
    if (pos == std::string::npos) {
        return "";
    }
    std::size_t start = pos + key.size();
    std::size_t end = cookie.find(';', start);
    if (end == std::string::npos) {
        end = cookie.size();
    }
    return cookie.substr(start, end - start);
}

}  // namespace

AuthHandler::AuthHandler(
    application::AuthService& auth_service,
    infrastructure::session::SessionManager& session_manager
)
    : auth_service_(auth_service)
    , session_manager_(session_manager) {}

bool AuthHandler::is_user_logged_in(const presentation::http::HttpRequest& request) const {
    return session_manager_.validate_session(read_session_id(request));
}

bool AuthHandler::is_merchant_logged_in(const presentation::http::HttpRequest& request) const {
    return session_manager_.validate_session(read_session_id(request));
}

void AuthHandler::handle_user_register(const presentation::http::HttpRequest& request,
                                       presentation::http::HttpResponse& response) {
    json j = parse_body(request.body);
    std::string username = json_get_string(j, "username", "");
    std::string password = json_get_string(j, "password", "");

    try {
        UserDto dto = auth_service_.register_user(username, password);
        write_json(response, json{{"success", true}, {"id", dto.id}, {"username", dto.username}});
    } catch (const AppException& e) {
        write_error(response, status_for_auth_error(e), e.what());
    }
}

void AuthHandler::handle_user_login(const presentation::http::HttpRequest& request,
                                    presentation::http::HttpResponse& response) {
    json j = parse_body(request.body);
    std::string username = json_get_string(j, "username", "");
    std::string password = json_get_string(j, "password", "");

    try {
        std::string session_id = auth_service_.login_user(username, password);
        response.set_header("Set-Cookie", "session_id=" + session_id + "; Path=/; HttpOnly");
        // [DIAG][会话] 登录成功诊断日志：确认会话已创建并已下发 Set-Cookie
        Logger::instance().info("handle_user_login: ok, session issued, sid=" +
                                session_id.substr(0, 8) + "...");
        write_json(response, json{{"success", true}, {"session_id", session_id}});
    } catch (const AppException& e) {
        write_error(response, status_for_auth_error(e), e.what());
    }
}

void AuthHandler::handle_user_info(const presentation::http::HttpRequest& request,
                                   presentation::http::HttpResponse& response) {
    // ============================================================
    // [DIAG][会话] 登录校验诊断日志（排查“登录后立即被踢 / 会话丢失”）
    // 判读：
    //   sid=<empty>           -> Cookie 未携带（多为浏览器端问题）
    //   sid=xxxxxxxx... + 401 -> 会话失效或用户不存在（详见 AuthService::get_user_info 日志）
    // ============================================================
    const std::string sid = read_session_id(request);
    Logger::instance().info("handle_user_info: sid=" +
                            (sid.empty() ? std::string("<empty>") : sid.substr(0, 8) + "..."));

    auto info = auth_service_.get_user_info(sid);
    if (!info) {
        Logger::instance().warn("handle_user_info: -> 401 (会话无效或用户不存在)");
        write_error(response, 401, "未登录或会话已过期");
        return;
    }
    write_json(response, json{
        {"success", true},
        {"id", info->id},
        {"user_id", info->id},   // 别名：前端 checkLogin 依赖 user_id 判定登录态
        {"username", info->username},
        {"role", static_cast<int>(info->role)},
        {"active", info->active}
    });
}

void AuthHandler::handle_user_logout(const presentation::http::HttpRequest& request,
                                     presentation::http::HttpResponse& response) {
    auth_service_.logout_user(read_session_id(request));
    response.set_header("Set-Cookie", "session_id=; Path=/; Max-Age=0");
    write_json(response, json{{"success", true}});
}

void AuthHandler::handle_merchant_register(const presentation::http::HttpRequest& request,
                                           presentation::http::HttpResponse& response) {
    json j = parse_body(request.body);
    std::string username = json_get_string(j, "username", "");
    std::string password = json_get_string(j, "password", "");
    std::string shop_name = json_get_string(j, "shop_name", "");
    std::string address = json_get_string(j, "address", "");

    try {
        int id = auth_service_.register_merchant(username, password, shop_name, address);
        write_json(response, json{{"success", true}, {"id", id}});
    } catch (const AppException& e) {
        write_error(response, status_for_auth_error(e), e.what());
    }
}

void AuthHandler::handle_merchant_login(const presentation::http::HttpRequest& request,
                                        presentation::http::HttpResponse& response) {
    json j = parse_body(request.body);
    std::string username = json_get_string(j, "username", "");
    std::string password = json_get_string(j, "password", "");

    try {
        std::string session_id = auth_service_.login_merchant(username, password);
        response.set_header("Set-Cookie", "session_id=" + session_id + "; Path=/; HttpOnly");
        write_json(response, json{{"success", true}, {"session_id", session_id}});
    } catch (const AppException& e) {
        write_error(response, status_for_auth_error(e), e.what());
    }
}

void AuthHandler::handle_merchant_info(const presentation::http::HttpRequest& request,
                                       presentation::http::HttpResponse& response) {
    auto info = auth_service_.get_merchant_info(read_session_id(request));
    if (!info) {
        write_error(response, 401, "未登录或会话已过期");
        return;
    }
    write_json(response, json{
        {"success", true},
        {"id", info->get_id()},
        {"username", info->get_username()},
        {"shop_name", info->get_shop_name()},
        {"address", info->get_address()},
        {"status", info->is_open() ? 1 : 0}
    });
}

void AuthHandler::handle_merchant_status(const presentation::http::HttpRequest& request,
                                         presentation::http::HttpResponse& response) {
    json j = parse_body(request.body);
    bool open = json_get_int(j, "status", 0) != 0;

    bool ok = auth_service_.set_merchant_status(read_session_id(request), open);
    if (!ok) {
        write_error(response, 401, "未登录或商家不存在");
        return;
    }
    write_json(response, json{{"success", true}, {"status", open ? 1 : 0}});
}

void AuthHandler::handle_merchant_logout(const presentation::http::HttpRequest& request,
                                         presentation::http::HttpResponse& response) {
    auth_service_.logout_user(read_session_id(request));
    response.set_header("Set-Cookie", "session_id=; Path=/; Max-Age=0");
    write_json(response, json{{"success", true}});
}

void AuthHandler::handle_get_shops(const presentation::http::HttpRequest& request,
                                   presentation::http::HttpResponse& response) {
    (void)request;
    // 受保护读：有界队列 + 熔断器 + 高水位降级（队列>80% → 仅查本地缓存）
    auto result = auth_service_.get_open_merchants_guarded();
    switch (result.status) {
        case shared::RequestGuard::Result::kShed:
            // 高水位快速降级：本地缓存数据或空数组，直接输出（体始终有值）
            response.set_status(200);
            response.set_json(std::move(*result.body));
            return;
        case shared::RequestGuard::Result::kRejected:
            // 熔断/队列超时：简单错误页（非复杂 JSON、不打 ERROR 日志），快速失败
            write_overload_page(response);
            return;
        case shared::RequestGuard::Result::kOk:
        default:
            break;
    }
    if (result.body) {
        // 前端 loadShops() 期望直接返回数组（与订单列表接口一致），不能包裹在对象里。
        // 已由 AuthService 序列化（含多级缓存命中），此处直接作为响应体，零二次序列化。
        response.set_status(200);
        response.set_json(std::move(*result.body));
        return;
    }
    write_error(response, 500, "查询商家失败，请稍后重试");
}

}  // namespace presentation::handlers
