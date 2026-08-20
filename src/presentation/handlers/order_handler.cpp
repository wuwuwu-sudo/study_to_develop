#include "presentation/handlers/order_handler.h"

#include <nlohmann/json.hpp>

#include <optional>

#include "infrastructure/common/exception.h"

namespace presentation::handlers {

namespace {

using infrastructure::common::AppException;
using nlohmann::json;

// 解析请求体为 JSON，失败时返回空对象
json parse_body(const std::string& body) {
    try {
        return json::parse(body);
    } catch (...) {
        return json::object();
    }
}

int json_get_int(const json& j, const char* key, int default_value) {
    if (j.contains(key) && j[key].is_number_integer()) {
        return j[key].get<int>();
    }
    return default_value;
}

std::string json_get_string(const json& j, const char* key, const std::string& default_value) {
    if (j.contains(key) && j[key].is_string()) {
        return j[key].get<std::string>();
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

// 将订单服务 AppException 映射为 HTTP 状态码
int status_for_order_error(const AppException& e) {
    const std::string msg = e.what();
    if (msg.find("不存在") != std::string::npos) {
        return 404;
    }
    if (msg.find("不能为空") != std::string::npos ||
        msg.find("无效") != std::string::npos ||
        msg.find("不能少于") != std::string::npos ||
        msg.find("已下架") != std::string::npos ||
        msg.find("不属于") != std::string::npos ||
        msg.find("未营业") != std::string::npos) {
        return 400;
    }
    return 500;
}

// 读取会话 ID：优先 session_id 请求头，其次 Cookie（浏览器回传）
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

// 将 0-4 的整数订单状态映射为 OrderStatus；非法值返回 nullopt
std::optional<OrderStatus> order_status_from_int(int value) {
    if (value < 0 || value > static_cast<int>(OrderStatus::CANCELLED)) {
        return std::nullopt;
    }
    return static_cast<OrderStatus>(value);
}

// 将订单 DTO 序列化为前端友好的 JSON（含 qty/customer_id 等兼容字段）
json order_to_json(const OrderDto& order) {
    json items = json::array();
    for (const auto& item : order.items) {
        items.push_back({
            {"dish_id", item.dish_id},
            {"dish_name", item.dish_name},
            {"price", item.price},
            {"quantity", item.quantity},
            {"qty", item.quantity}
        });
    }
    return json{
        {"id", order.id},
        {"user_id", order.user_id},
        {"customer_id", order.user_id},
        {"merchant_id", order.merchant_id},
        {"status", static_cast<int>(order.status)},
        {"total", order.total},
        {"address", order.address},
        {"remark", order.remark},
        {"note", order.remark},
        {"created_at", order.created_at},
        {"items", items}
    };
}

}  // namespace

OrderHandler::OrderHandler(
    application::OrderService& order_service,
    application::AuthService& auth_service,
    infrastructure::session::SessionManager& session_manager
)
    : order_service_(order_service)
    , auth_service_(auth_service)
    , session_manager_(session_manager) {}

void OrderHandler::handle_order_submit(const presentation::http::HttpRequest& request,
                                       presentation::http::HttpResponse& response) {
    json j = parse_body(request.body);

    // 1. 会话校验：必须为已登录用户
    auto user = auth_service_.get_user_info(read_session_id(request));
    if (!user) {
        write_error(response, 401, "未登录或会话已过期");
        return;
    }

    // 2. 解析请求体（前端 items: [{dish_id, qty}]）
    int merchant_id = json_get_int(j, "merchant_id", 0);
    std::string address = json_get_string(j, "address", "");
    std::vector<OrderItemDto> items;
    if (j.contains("items") && j["items"].is_array()) {
        for (const auto& it : j["items"]) {
            OrderItemDto item;
            item.dish_id = json_get_int(it, "dish_id", 0);
            item.quantity = json_get_int(it, "qty", json_get_int(it, "quantity", 0));
            items.push_back(item);
        }
    }

    // 3. 防御性参数校验
    if (merchant_id <= 0) {
        write_error(response, 400, "merchant_id 无效");
        return;
    }
    if (address.empty()) {
        write_error(response, 400, "收货地址不能为空");
        return;
    }
    if (items.empty()) {
        write_error(response, 400, "购物车为空");
        return;
    }

    // 4. 调服务层（服务层做用户/商家/菜品存在性、归属与服务器端定价校验）
    try {
        int order_id = order_service_.create_order(user->id, merchant_id, items, address);
        write_json(response, json{{"success", true}, {"order_id", order_id}});
    } catch (const AppException& e) {
        write_error(response, status_for_order_error(e), e.what());
    }
}

void OrderHandler::handle_update_order_status(const presentation::http::HttpRequest& request,
                                              presentation::http::HttpResponse& response) {
    json j = parse_body(request.body);

    // 1. 会话校验：必须为已登录商家
    auto merchant = auth_service_.get_merchant_info(read_session_id(request));
    if (!merchant) {
        write_error(response, 401, "未登录或会话已过期");
        return;
    }

    // 2. 解析与校验
    int order_id = json_get_int(j, "order_id", 0);
    int status_int = json_get_int(j, "status", -1);
    if (order_id <= 0) {
        write_error(response, 400, "order_id 无效");
        return;
    }
    auto new_status = order_status_from_int(status_int);
    if (!new_status) {
        write_error(response, 400, "订单状态无效");
        return;
    }

    // 3. 调服务层（服务层校验商家归属与状态机合法性）
    bool ok = order_service_.update_order_status(order_id, merchant->get_id(), *new_status);
    if (!ok) {
        write_error(response, 400, "更新订单状态失败（非法转换或无权操作）");
        return;
    }
    write_json(response, json{{"success", true}});
}

void OrderHandler::handle_confirm_delivery(const presentation::http::HttpRequest& request,
                                           presentation::http::HttpResponse& response) {
    json j = parse_body(request.body);

    // 1. 会话校验：必须为已登录用户（顾客）
    auto user = auth_service_.get_user_info(read_session_id(request));
    if (!user) {
        write_error(response, 401, "未登录或会话已过期");
        return;
    }

    // 2. 解析与校验
    int order_id = json_get_int(j, "order_id", 0);
    if (order_id <= 0) {
        write_error(response, 400, "order_id 无效");
        return;
    }

    // 3. 调服务层（服务层校验顾客归属与状态机：仅配送中 → 已完成）
    bool ok = order_service_.complete_order(order_id, user->id);
    if (!ok) {
        write_error(response, 400, "确认收货失败（订单不存在、非本人订单或状态不允许）");
        return;
    }
    write_json(response, json{{"success", true}});
}

void OrderHandler::handle_my_orders(const presentation::http::HttpRequest& request,
                                    presentation::http::HttpResponse& response) {
    auto user = auth_service_.get_user_info(read_session_id(request));
    if (!user) {
        write_error(response, 401, "未登录或会话已过期");
        return;
    }
    try {
        std::vector<OrderDto> orders = order_service_.get_orders_for_user(user->id);
        json arr = json::array();
        for (const auto& o : orders) {
            arr.push_back(order_to_json(o));
        }
        write_json(response, arr);  // 前端期望直接返回数组
    } catch (const AppException& e) {
        write_error(response, status_for_order_error(e), e.what());
    }
}

void OrderHandler::handle_merchant_orders(const presentation::http::HttpRequest& request,
                                          presentation::http::HttpResponse& response) {
    auto merchant = auth_service_.get_merchant_info(read_session_id(request));
    if (!merchant) {
        write_error(response, 401, "未登录或会话已过期");
        return;
    }
    try {
        std::vector<OrderDto> orders = order_service_.get_orders_for_merchant(merchant->get_id());
        json arr = json::array();
        for (const auto& o : orders) {
            arr.push_back(order_to_json(o));
        }
        write_json(response, arr);  // 前端期望直接返回数组
    } catch (const AppException& e) {
        write_error(response, status_for_order_error(e), e.what());
    }
}

}  // namespace presentation::handlers
