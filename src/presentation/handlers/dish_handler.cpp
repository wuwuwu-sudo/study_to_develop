#include "presentation/handlers/dish_handler.h"

#include <charconv>
#include <string>

#include <nlohmann/json.hpp>

#include "infrastructure/common/exception.h"
#include "presentation/handlers/overload_page.h"

namespace presentation::handlers {

namespace {

using infrastructure::common::AppException;
using nlohmann::json;

// 解析可选整数查询参数：空串或非法 → 默认值；合法 → 解析值。
// 用 std::from_chars（C++17 不抛异常），避免可选参数缺失时 stoi("") 抛
// invalid_argument + 栈展开的热路径成本（perf v3 已定位该开销）。
int parse_int_or_default(const std::string& s, int default_value) {
    if (s.empty()) {
        return default_value;
    }
    int value = 0;
    const char* begin = s.data();
    const char* end = s.data() + s.size();
    auto [ptr, ec] = std::from_chars(begin, end, value);
    if (ec == std::errc() && ptr == end) {
        return value;
    }
    return default_value;
}

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

double json_get_double(const json& j, const char* key, double default_value) {
    if (j.contains(key) && j[key].is_number()) {
        return j[key].get<double>();
    }
    return default_value;
}

std::string json_get_string(const json& j, const char* key,
                            const std::string& default_value) {
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

// 将服务层 AppException 映射为 HTTP 状态码并写出错误响应
void write_service_error(const AppException& e,
                         presentation::http::HttpResponse& response) {
    const std::string msg = e.what();
    int status = 500;
    if (msg.find("不存在") != std::string::npos) {
        status = 404;
    } else if (msg.find("不能为空") != std::string::npos ||
               msg.find("不能为负数") != std::string::npos ||
               msg.find("无效") != std::string::npos) {
        status = 400;
    }
    write_error(response, status, msg);
}

}  // namespace

DishHandler::DishHandler(
    application::DishService& dish_service,
    application::AuthService& auth_service
)
    : dish_service_(dish_service)
    , auth_service_(auth_service) {}

void DishHandler::handle_get_dishes(const presentation::http::HttpRequest& request,
                                    presentation::http::HttpResponse& response) {
    int merchant_id = 0;
    try {
        merchant_id = std::stoi(request.query("merchant_id"));
    } catch (...) {
        write_error(response, 400, "merchant_id 参数缺失或无效");
        return;
    }

    int page = parse_int_or_default(request.query("page"), 1);
    int page_size = parse_int_or_default(request.query("page_size"), 10);
    if (page < 1) {
        page = 1;
    }
    if (page_size < 1) {
        page_size = 10;
    }

    // 受保护读：有界队列 + 熔断器 + 高水位降级（队列>80% → 仅查本地缓存）
    auto result = dish_service_.get_dishes_paged_guarded(merchant_id, page, page_size);
    switch (result.status) {
        case shared::RequestGuard::Result::kShed:
            // 高水位快速降级：本地缓存数据或空成功响应，直接输出（体始终有值）
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
        // 缓存命中/序列化结果直接作为响应体，零二次序列化（对齐 /api/shops 字符串直出）
        response.set_status(200);
        response.set_json(std::move(*result.body));
        return;
    }
    write_error(response, 500, "查询菜品失败，请稍后重试");
}

void DishHandler::handle_add_dish(const presentation::http::HttpRequest& request,
                                  presentation::http::HttpResponse& response) {
    json j = parse_body(request.body);

    DishDto dto;
    dto.merchant_id = json_get_int(j, "merchant_id", 0);
    dto.name = json_get_string(j, "name", "");
    dto.price = json_get_double(j, "price", -1.0);
    dto.category = json_get_string(j, "category", "");
    dto.description = json_get_string(j, "description", "");

    if (dto.merchant_id <= 0) {
        write_error(response, 400, "merchant_id 不能为空");
        return;
    }
    if (dto.name.empty()) {
        write_error(response, 400, "菜品名称不能为空");
        return;
    }
    if (dto.price < 0) {
        write_error(response, 400, "菜品价格不能为负数");
        return;
    }

    try {
        DishDto created = dish_service_.create_dish(dto);
        write_json(response, json{{"success", true}, {"id", created.id}});
    } catch (const AppException& e) {
        write_service_error(e, response);
    }
}

void DishHandler::handle_edit_dish(const presentation::http::HttpRequest& request,
                                   presentation::http::HttpResponse& response) {
    json j = parse_body(request.body);

    DishDto dto;
    dto.id = json_get_int(j, "id", 0);
    dto.name = json_get_string(j, "name", "");
    dto.price = json_get_double(j, "price", -1.0);
    dto.category = json_get_string(j, "category", "");
    dto.description = json_get_string(j, "description", "");

    if (dto.id <= 0) {
        write_error(response, 400, "id 不能为空");
        return;
    }
    if (dto.name.empty()) {
        write_error(response, 400, "菜品名称不能为空");
        return;
    }
    if (dto.price < 0) {
        write_error(response, 400, "菜品价格不能为负数");
        return;
    }

    try {
        DishDto updated = dish_service_.update_dish(dto);
        write_json(response, json{{"success", true}, {"id", updated.id}});
    } catch (const AppException& e) {
        write_service_error(e, response);
    }
}

void DishHandler::handle_toggle_available(const presentation::http::HttpRequest& request,
                                          presentation::http::HttpResponse& response) {
    json j = parse_body(request.body);

    int dish_id = json_get_int(j, "id", 0);
    bool available = true;
    if (j.contains("available")) {
        available = j["available"].get<bool>();
    }

    if (dish_id <= 0) {
        write_error(response, 400, "id 不能为空");
        return;
    }

    bool ok = dish_service_.set_available(dish_id, available);
    if (!ok) {
        write_error(response, 404, "菜品不存在或已删除");
        return;
    }
    write_json(response, json{{"success", true}});
}

void DishHandler::handle_delete_dish(const presentation::http::HttpRequest& request,
                                     presentation::http::HttpResponse& response) {
    json j = parse_body(request.body);

    int dish_id = json_get_int(j, "id", 0);
    if (dish_id <= 0) {
        write_error(response, 400, "id 不能为空");
        return;
    }

    bool ok = dish_service_.soft_delete(dish_id);
    if (!ok) {
        write_error(response, 404, "菜品不存在或已删除");
        return;
    }
    write_json(response, json{{"success", true}});
}

}  // namespace presentation::handlers
