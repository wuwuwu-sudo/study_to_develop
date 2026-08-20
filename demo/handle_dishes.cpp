#include "handle_dishes.h"

/* ================= 菜品 API ================= */
void handle_get_dishes(int fd, const std::string& request) {
    sqlite3* db = nullptr;
    sqlite3_open("food_delivery.db", &db);

    /* ========== 1. 从原始请求中解析 merchant_id ========== */
    int merchant_id = -1;
    bool is_customer_request = false;
    
    // 从请求行获取完整路径
    size_t method_end = request.find(' ');
    size_t path_end = request.find(' ', method_end + 1);
    std::string full_path = request.substr(method_end + 1, path_end - method_end - 1);
    
    fprintf(stderr, "[GET DISHES] full_path: '%s'\n", full_path.c_str());
    
    // 检查是否有查询参数
    size_t query_pos = full_path.find('?');
    if (query_pos != std::string::npos) {
        std::string query_string = full_path.substr(query_pos + 1);
        fprintf(stderr, "[GET DISHES] query_string: '%s'\n", query_string.c_str());
        
        // 解析 merchant_id=xxx
        size_t merchant_pos = query_string.find("merchant_id=");
        if (merchant_pos != std::string::npos) {
            // ✅ "merchant_id=" 的长度是 12
            merchant_pos += 12;  // 跳过 "merchant_id="
            
            size_t end_pos = query_string.find('&', merchant_pos);
            if (end_pos == std::string::npos) {
                end_pos = query_string.length();
            }
            std::string merchant_id_str = query_string.substr(merchant_pos, end_pos - merchant_pos);
            
            // 去除空白字符
            merchant_id_str.erase(0, merchant_id_str.find_first_not_of(" \t\r\n"));
            merchant_id_str.erase(merchant_id_str.find_last_not_of(" \t\r\n") + 1);
            
            fprintf(stderr, "[GET DISHES] merchant_id_str: '%s'\n", merchant_id_str.c_str());
            
            if (!merchant_id_str.empty()) {
                try {
                    merchant_id = std::stoi(merchant_id_str);
                    is_customer_request = true;
                    fprintf(stderr, "[GET DISHES] 顾客请求: merchant_id=%d\n", merchant_id);
                } catch (const std::exception& e) {
                    fprintf(stderr, "[GET DISHES] 解析 merchant_id 失败: %s\n", e.what());
                }
            } else {
                fprintf(stderr, "[GET DISHES] merchant_id_str 为空\n");
            }
        } else {
            fprintf(stderr, "[GET DISHES] 未找到 merchant_id 参数\n");
        }
    } else {
        fprintf(stderr, "[GET DISHES] 没有查询参数\n");
    }

    /* ========== 2. 如果是商家请求（无参数），从 session 获取 ========== */
    if (!is_customer_request || merchant_id <= 0) {
        merchant_id = get_merchant_id_by_session(request);
        if (merchant_id > 0) {
            fprintf(stderr, "[GET DISHES] 商家请求: merchant_id=%d\n", merchant_id);
        }
    }

    /* ========== 3. 如果还是没有 merchant_id，返回错误 ========== */
    if (merchant_id <= 0) {
        fprintf(stderr, "[GET DISHES] ❌ 无法获取 merchant_id\n");
        const char* resp =
            "HTTP/1.1 400 Bad Request\r\n"
            "Content-Type: application/json; charset=utf-8\r\n"
            "Content-Length: 57\r\n"
            "Connection: close\r\n"
            "\r\n"
            "{\"success\":false,\"code\":40001,\"message\":\"缺少商家ID\"}";
        write(fd, resp, strlen(resp));
        sqlite3_close(db);
        return;
    }

    /* ========== 4. 查询菜品 ========== */
    const char* sql;
    if (is_customer_request) {
        sql = "SELECT id, name, price, category, description, available "
              "FROM dishes "
              "WHERE merchant_id = ? AND deleted = 0 AND available = 1";
    } else {
        sql = "SELECT id, name, price, category, description, available "
              "FROM dishes "
              "WHERE merchant_id = ? AND deleted = 0";
    }

    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, merchant_id);

    std::ostringstream body;
    
    if (is_customer_request) {
        body << "[";
    } else {
        body << "{\"dishes\":[";
    }

    bool first = true;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        if (!first) body << ',';
        first = false;

        int id = sqlite3_column_int(stmt, 0);
        const char* name = (const char*)sqlite3_column_text(stmt, 1);
        double price = sqlite3_column_double(stmt, 2);
        const char* category = (const char*)sqlite3_column_text(stmt, 3);
        const char* description = (const char*)sqlite3_column_text(stmt, 4);
        int available = sqlite3_column_int(stmt, 5);

        auto escape_json = [](const std::string& s) {
            std::string result;
            for (char c : s) {
                if (c == '"') result += "\\\"";
                else if (c == '\\') result += "\\\\";
                else if (c == '\n') result += "\\n";
                else if (c == '\r') result += "\\r";
                else if (c == '\t') result += "\\t";
                else result += c;
            }
            return result;
        };

        std::string safe_name = name ? name : "";
        std::string safe_category = category ? category : "";
        std::string safe_description = description ? description : "";

        body << '{'
             << "\"id\":" << id << ','
             << "\"name\":\"" << escape_json(safe_name) << "\","
             << "\"price\":" << price << ','
             << "\"category\":\"" << escape_json(safe_category) << "\","
             << "\"description\":\"" << escape_json(safe_description) << "\","
             << "\"available\":" << available
             << '}';
    }

    if (is_customer_request) {
        body << "]";
    } else {
        body << "]}";
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);

    /* ========== 5. 返回响应 ========== */
    std::string body_str = body.str();
    std::string resp =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json; charset=utf-8\r\n"
        "Content-Length: " + std::to_string(body_str.size()) + "\r\n"
        "Connection: close\r\n"
        "\r\n" +
        body_str;

    write(fd, resp.c_str(), resp.size());

    fprintf(stderr, "[GET DISHES] merchant_id=%d, is_customer=%d\n", 
            merchant_id, is_customer_request);
}

void handle_add_dish(int fd, const std::string& request) {
    sqlite3* db = nullptr;
    sqlite3_open("food_delivery.db", &db);

    /* ========== 1. 获取 merchant_id ========== */
    int merchant_id = get_merchant_id_by_session(request);
    if (merchant_id <= 0) {
        const char* resp =
            "HTTP/1.1 401 Unauthorized\r\n"
            "Content-Type: application/json; charset=utf-8\r\n"
            "Content-Length: 54\r\n"
            "Connection: close\r\n"
            "\r\n"
            "{\"success\":false,\"code\":40101,\"message\":\"未登录\"}";
        write(fd, resp, strlen(resp));
        sqlite3_close(db);
        return;
    }

    /* ========== 2. 解析 JSON Body ========== */
    size_t body_pos = request.find("\r\n\r\n");
    if (body_pos == std::string::npos) {
        const char* resp =
            "HTTP/1.1 400 Bad Request\r\n"
            "Content-Type: application/json; charset=utf-8\r\n"
            "Content-Length: 57\r\n"
            "Connection: close\r\n"
            "\r\n"
            "{\"success\":false,\"code\":40001,\"message\":\"无效的请求格式\"}";
        write(fd, resp, strlen(resp));
        sqlite3_close(db);
        return;
    }

    std::string body = request.substr(body_pos + 4);
    json req;
    try {
        req = json::parse(body);
    } catch (...) {
        const char* resp =
            "HTTP/1.1 400 Bad Request\r\n"
            "Content-Type: application/json; charset=utf-8\r\n"
            "Content-Length: 57\r\n"
            "Connection: close\r\n"
            "\r\n"
            "{\"success\":false,\"code\":40002,\"message\":\"JSON解析失败\"}";
        write(fd, resp, strlen(resp));
        sqlite3_close(db);
        return;
    }

    /* ========== 3. 校验参数 ========== */
    if (!req.contains("name") || !req["name"].is_string() ||
        req["name"].get<std::string>().empty()) {
        const char* resp =
            "HTTP/1.1 400 Bad Request\r\n"
            "Content-Type: application/json; charset=utf-8\r\n"
            "Content-Length: 57\r\n"
            "Connection: close\r\n"
            "\r\n"
            "{\"success\":false,\"code\":40003,\"message\":\"请输入菜品名称\"}";
        write(fd, resp, strlen(resp));
        sqlite3_close(db);
        return;
    }
    if (!req.contains("price") || !req["price"].is_number()) {
        const char* resp =
            "HTTP/1.1 400 Bad Request\r\n"
            "Content-Type: application/json; charset=utf-8\r\n"
            "Content-Length: 57\r\n"
            "Connection: close\r\n"
            "\r\n"
            "{\"success\":false,\"code\":40004,\"message\":\"价格格式错误\"}";
        write(fd, resp, strlen(resp));
        sqlite3_close(db);
        return;
    }

    double price = req["price"].get<double>();
    if (price < 0) {
        const char* resp =
            "HTTP/1.1 400 Bad Request\r\n"
            "Content-Type: application/json; charset=utf-8\r\n"
            "Content-Length: 58\r\n"
            "Connection: close\r\n"
            "\r\n"
            "{\"success\":false,\"code\":40005,\"message\":\"价格不能为负数\"}";
        write(fd, resp, strlen(resp));
        sqlite3_close(db);
        return;
    }

    std::string name = req["name"];
    std::string category = req.value("category", "");
    std::string description = req.value("description", "");

    /* ========== 4. 查重（按商家） ========== */
    const char* check_sql =
        "SELECT COUNT(*) FROM dishes WHERE merchant_id = ? AND name = ? AND deleted = 0";

    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db, check_sql, -1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, merchant_id);
    sqlite3_bind_text(stmt, 2, name.c_str(), -1, SQLITE_TRANSIENT);

    int count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);

    if (count > 0) {
        const char* resp =
            "HTTP/1.1 409 Conflict\r\n"
            "Content-Type: application/json; charset=utf-8\r\n"
            "Content-Length: 61\r\n"
            "Connection: close\r\n"
            "\r\n"
            "{\"success\":false,\"code\":40901,\"message\":\"菜品名称已存在\"}";
        write(fd, resp, strlen(resp));
        sqlite3_close(db);
        return;
    }

    /* ========== 5. 插入菜品 ========== */
    const char* insert_sql =
        "INSERT INTO dishes "
        "(merchant_id, name, price, category, description, available, deleted) "
        "VALUES (?, ?, ?, ?, ?, 1, 0);";

    sqlite3_prepare_v2(db, insert_sql, -1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, merchant_id);
    sqlite3_bind_text(stmt, 2, name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 3, price);
    sqlite3_bind_text(stmt, 4, category.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, description.c_str(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        const char* resp =
            "HTTP/1.1 500 Internal Server Error\r\n"
            "Content-Type: application/json; charset=utf-8\r\n"
            "Content-Length: 58\r\n"
            "Connection: close\r\n"
            "\r\n"
            "{\"success\":false,\"code\":50001,\"message\":\"菜品添加失败\"}";
        write(fd, resp, strlen(resp));
        sqlite3_finalize(stmt);
        sqlite3_close(db);
        return;
    }

    int dish_id = static_cast<int>(sqlite3_last_insert_rowid(db));
    sqlite3_finalize(stmt);
    sqlite3_close(db);

    /* ========== 6. 成功返回 ========== */
    std::string response_body = 
        "{\"success\":true,\"code\":0,\"message\":\"菜品添加成功\",\"dish_id\":" + 
        std::to_string(dish_id) + "}";
    std::string resp =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json; charset=utf-8\r\n"
        "Content-Length: " + std::to_string(response_body.length()) + "\r\n"
        "Connection: close\r\n"
        "\r\n" +
        response_body;

    write(fd, resp.c_str(), resp.size());

    fprintf(stderr, "[ADD DISH] merchant_id=%d dish_id=%d name=%s price=%.2f\n",
            merchant_id, dish_id, name.c_str(), price);
}

void handle_edit_dish(int fd, const std::string& request) {
    sqlite3* db = nullptr;
    sqlite3_open("food_delivery.db", &db);

    /* ========== 1. 获取 merchant_id ========== */
    int merchant_id = get_merchant_id_by_session(request);
    if (merchant_id <= 0) {
        const char* resp =
            "HTTP/1.1 401 Unauthorized\r\n"
            "Content-Type: application/json; charset=utf-8\r\n"
            "Content-Length: 54\r\n"
            "Connection: close\r\n"
            "\r\n"
            "{\"success\":false,\"code\":40101,\"message\":\"未登录\"}";
        write(fd, resp, strlen(resp));
        sqlite3_close(db);
        return;
    }

    /* ========== 2. 解析 JSON Body ========== */
    size_t body_pos = request.find("\r\n\r\n");
    if (body_pos == std::string::npos) {
        const char* resp =
            "HTTP/1.1 400 Bad Request\r\n"
            "Content-Type: application/json; charset=utf-8\r\n"
            "Content-Length: 57\r\n"
            "Connection: close\r\n"
            "\r\n"
            "{\"success\":false,\"code\":40001,\"message\":\"无效的请求格式\"}";
        write(fd, resp, strlen(resp));
        sqlite3_close(db);
        return;
    }

    std::string body = request.substr(body_pos + 4);
    json req;
    try {
        req = json::parse(body);
    } catch (...) {
        const char* resp =
            "HTTP/1.1 400 Bad Request\r\n"
            "Content-Type: application/json; charset=utf-8\r\n"
            "Content-Length: 57\r\n"
            "Connection: close\r\n"
            "\r\n"
            "{\"success\":false,\"code\":40002,\"message\":\"JSON解析失败\"}";
        write(fd, resp, strlen(resp));
        sqlite3_close(db);
        return;
    }

    /* ========== 3. 校验参数 ========== */
    if (!req.contains("id") || !req["id"].is_number_integer() ||
        !req.contains("name") || !req["name"].is_string() ||
        !req.contains("price") || !req["price"].is_number()) {
        const char* resp =
            "HTTP/1.1 400 Bad Request\r\n"
            "Content-Type: application/json; charset=utf-8\r\n"
            "Content-Length: 57\r\n"
            "Connection: close\r\n"
            "\r\n"
            "{\"success\":false,\"code\":40003,\"message\":\"参数不完整或价格格式错误\"}";
        write(fd, resp, strlen(resp));
        sqlite3_close(db);
        return;
    }

    int dish_id = req["id"];
    std::string name = req["name"];
    double price = req["price"].get<double>();
    if (price < 0) {
        const char* resp =
            "HTTP/1.1 400 Bad Request\r\n"
            "Content-Type: application/json; charset=utf-8\r\n"
            "Content-Length: 58\r\n"
            "Connection: close\r\n"
            "\r\n"
            "{\"success\":false,\"code\":40004,\"message\":\"价格不能为负数\"}";
        write(fd, resp, strlen(resp));
        sqlite3_close(db);
        return;
    }
    std::string category = req.value("category", "");
    std::string description = req.value("description", "");

    /* ========== 4. 校验菜品归属 ========== */
    const char* owner_sql =
        "SELECT COUNT(*) FROM dishes WHERE id = ? AND merchant_id = ? AND deleted = 0";
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db, owner_sql, -1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, dish_id);
    sqlite3_bind_int(stmt, 2, merchant_id);

    int owner_ok = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        owner_ok = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);

    if (!owner_ok) {
        const char* resp =
            "HTTP/1.1 403 Forbidden\r\n"
            "Content-Type: application/json; charset=utf-8\r\n"
            "Content-Length: 58\r\n"
            "Connection: close\r\n"
            "\r\n"
            "{\"success\":false,\"code\":40301,\"message\":\"无权限修改\"}";
        write(fd, resp, strlen(resp));
        sqlite3_close(db);
        return;
    }

    /* ========== 5. 查重名（排除自己） ========== */
    const char* dup_sql =
        "SELECT COUNT(*) FROM dishes WHERE merchant_id = ? AND name = ? AND id != ? AND deleted = 0";
    sqlite3_prepare_v2(db, dup_sql, -1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, merchant_id);
    sqlite3_bind_text(stmt, 2, name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, dish_id);

    int dup_count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        dup_count = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);

    if (dup_count > 0) {
        const char* resp =
            "HTTP/1.1 409 Conflict\r\n"
            "Content-Type: application/json; charset=utf-8\r\n"
            "Content-Length: 61\r\n"
            "Connection: close\r\n"
            "\r\n"
            "{\"success\":false,\"code\":40901,\"message\":\"菜品名称已存在\"}";
        write(fd, resp, strlen(resp));
        sqlite3_close(db);
        return;
    }

    /* ========== 6. 更新菜品 ========== */
    const char* update_sql =
        "UPDATE dishes SET name = ?, price = ?, category = ?, description = ? "
        "WHERE id = ? AND merchant_id = ?";

    sqlite3_prepare_v2(db, update_sql, -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 2, price);
    sqlite3_bind_text(stmt, 3, category.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, description.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 5, dish_id);
    sqlite3_bind_int(stmt, 6, merchant_id);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        const char* resp =
            "HTTP/1.1 500 Internal Server Error\r\n"
            "Content-Type: application/json; charset=utf-8\r\n"
            "Content-Length: 58\r\n"
            "Connection: close\r\n"
            "\r\n"
            "{\"success\":false,\"code\":50001,\"message\":\"数据库更新失败\"}";
        write(fd, resp, strlen(resp));
        sqlite3_finalize(stmt);
        sqlite3_close(db);
        return;
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);

    /* ========== 7. 成功返回 ========== */
    std::string response_body = "{\"success\":true,\"code\":0,\"message\":\"菜品修改成功\"}";
    std::string resp =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json; charset=utf-8\r\n"
        "Content-Length: " + std::to_string(response_body.length()) + "\r\n"
        "Connection: close\r\n"
        "\r\n" +
        response_body;

    write(fd, resp.c_str(), resp.size());

    fprintf(stderr, "[EDIT DISH] merchant_id=%d dish_id=%d name=%s price=%.2f\n",
            merchant_id, dish_id, name.c_str(), price);
}

void handle_dish_available(int fd, const std::string& request)
{
    sqlite3* db = nullptr;
    sqlite3_open("food_delivery.db", &db);

    /* ========== 1. 获取 merchant_id ========== */
    int merchant_id = get_merchant_id_by_session(request);
    if (merchant_id <= 0) {
        const char* resp =
            "HTTP/1.1 401 Unauthorized\r\n"
            "Content-Type: application/json; charset=utf-8\r\n"
            "Content-Length: 54\r\n"
            "Connection: close\r\n"
            "\r\n"
            "{\"success\":false,\"code\":40101,\"message\":\"未登录\"}";
        write(fd, resp, strlen(resp));
        sqlite3_close(db);
        return;
    }

    /* ========== 2. 解析 JSON Body ========== */
    size_t body_pos = request.find("\r\n\r\n");
    if (body_pos == std::string::npos) {
        const char* resp =
            "HTTP/1.1 400 Bad Request\r\n"
            "Content-Type: application/json; charset=utf-8\r\n"
            "Content-Length: 57\r\n"
            "Connection: close\r\n"
            "\r\n"
            "{\"success\":false,\"code\":40001,\"message\":\"无效的请求格式\"}";
        write(fd, resp, strlen(resp));
        sqlite3_close(db);
        return;
    }

    std::string body = request.substr(body_pos + 4);
    json req;
    try {
        req = json::parse(body);
    } catch (...) {
        const char* resp =
            "HTTP/1.1 400 Bad Request\r\n"
            "Content-Type: application/json; charset=utf-8\r\n"
            "Content-Length: 57\r\n"
            "Connection: close\r\n"
            "\r\n"
            "{\"success\":false,\"code\":40002,\"message\":\"JSON解析失败\"}";
        write(fd, resp, strlen(resp));
        sqlite3_close(db);
        return;
    }

    /* ========== 3. 校验参数 ========== */
    int dish_id = -1;
    if (req.contains("dish_id") && req["dish_id"].is_number_integer()) {
        dish_id = req["dish_id"];
    } else if (req.contains("id") && req["id"].is_number_integer()) {
        dish_id = req["id"];
    } else {
        const char* resp =
            "HTTP/1.1 400 Bad Request\r\n"
            "Content-Type: application/json; charset=utf-8\r\n"
            "Content-Length: 63\r\n"
            "Connection: close\r\n"
            "\r\n"
            "{\"success\":false,\"code\":40003,\"message\":\"缺少 dish_id 或 id 参数\"}";
        write(fd, resp, strlen(resp));
        sqlite3_close(db);
        return;
    }

    int status = -1;
    if (req.contains("status") && req["status"].is_number_integer()) {
        status = req["status"];
    } else if (req.contains("available") && req["available"].is_boolean()) {
        status = req["available"] ? 1 : 0;
    } else {
        const char* resp =
            "HTTP/1.1 400 Bad Request\r\n"
            "Content-Type: application/json; charset=utf-8\r\n"
            "Content-Length: 66\r\n"
            "Connection: close\r\n"
            "\r\n"
            "{\"success\":false,\"code\":40004,\"message\":\"缺少 status 或 available 参数\"}";
        write(fd, resp, strlen(resp));
        sqlite3_close(db);
        return;
    }

    if (status != 0 && status != 1) {
        const char* resp =
            "HTTP/1.1 400 Bad Request\r\n"
            "Content-Type: application/json; charset=utf-8\r\n"
            "Content-Length: 58\r\n"
            "Connection: close\r\n"
            "\r\n"
            "{\"success\":false,\"code\":40005,\"message\":\"status 必须为 0 或 1\"}";
        write(fd, resp, strlen(resp));
        sqlite3_close(db);
        return;
    }

    /* ========== 4. 校验菜品归属 ========== */
    const char* owner_sql =
        "SELECT COUNT(*) FROM dishes WHERE id = ? AND merchant_id = ? AND deleted = 0";
    
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db, owner_sql, -1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, dish_id);
    sqlite3_bind_int(stmt, 2, merchant_id);

    int owner_ok = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        owner_ok = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);

    if (!owner_ok) {
        const char* resp =
            "HTTP/1.1 403 Forbidden\r\n"
            "Content-Type: application/json; charset=utf-8\r\n"
            "Content-Length: 58\r\n"
            "Connection: close\r\n"
            "\r\n"
            "{\"success\":false,\"code\":40301,\"message\":\"无权操作该菜品\"}";
        write(fd, resp, strlen(resp));
        sqlite3_close(db);
        return;
    }

    /* ========== 5. 更新菜品状态 ========== */
    const char* update_sql = "UPDATE dishes SET available = ? WHERE id = ? AND merchant_id = ?";
    sqlite3_stmt* update_stmt = nullptr;

    if (sqlite3_prepare_v2(db, update_sql, -1, &update_stmt, nullptr) != SQLITE_OK) {
        const char* resp =
            "HTTP/1.1 500 Internal Server Error\r\n"
            "Content-Type: application/json; charset=utf-8\r\n"
            "Content-Length: 58\r\n"
            "Connection: close\r\n"
            "\r\n"
            "{\"success\":false,\"code\":50001,\"message\":\"数据库更新失败\"}";
        write(fd, resp, strlen(resp));
        sqlite3_close(db);
        return;
    }

    sqlite3_bind_int(update_stmt, 1, status);
    sqlite3_bind_int(update_stmt, 2, dish_id);
    sqlite3_bind_int(update_stmt, 3, merchant_id);

    if (sqlite3_step(update_stmt) != SQLITE_DONE) {
        const char* resp =
            "HTTP/1.1 500 Internal Server Error\r\n"
            "Content-Type: application/json; charset=utf-8\r\n"
            "Content-Length: 58\r\n"
            "Connection: close\r\n"
            "\r\n"
            "{\"success\":false,\"code\":50002,\"message\":\"数据库更新失败\"}";
        write(fd, resp, strlen(resp));
        sqlite3_finalize(update_stmt);
        sqlite3_close(db);
        return;
    }

    sqlite3_finalize(update_stmt);
    sqlite3_close(db);

    /* ========== 6. 成功返回 ========== */
    std::string response_body = "{\"success\":true,\"code\":0,\"message\":\"菜品状态更新成功\"}";
    std::string resp =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json; charset=utf-8\r\n"
        "Content-Length: " + std::to_string(response_body.length()) + "\r\n"
        "Connection: close\r\n"
        "\r\n" +
        response_body;

    write(fd, resp.c_str(), resp.size());

    fprintf(stderr, "[AVAILABLE DISH] merchant_id=%d dish_id=%d status=%d\n",
            merchant_id, dish_id, status);
}

void handle_delete_dish(int fd, const std::string& request)
{
    sqlite3* db = nullptr;
    sqlite3_open("food_delivery.db", &db);

    /* ========== 1. 获取 merchant_id ========== */
    int merchant_id = get_merchant_id_by_session(request);
    if (merchant_id <= 0) {
        const char* resp =
            "HTTP/1.1 401 Unauthorized\r\n"
            "Content-Type: application/json; charset=utf-8\r\n"
            "Content-Length: 54\r\n"
            "Connection: close\r\n"
            "\r\n"
            "{\"success\":false,\"code\":40101,\"message\":\"未登录\"}";
        write(fd, resp, strlen(resp));
        sqlite3_close(db);
        return;
    }

    /* ========== 2. 解析 JSON Body ========== */
    size_t body_pos = request.find("\r\n\r\n");
    if (body_pos == std::string::npos) {
        const char* resp =
            "HTTP/1.1 400 Bad Request\r\n"
            "Content-Type: application/json; charset=utf-8\r\n"
            "Content-Length: 57\r\n"
            "Connection: close\r\n"
            "\r\n"
            "{\"success\":false,\"code\":40001,\"message\":\"无效的请求格式\"}";
        write(fd, resp, strlen(resp));
        sqlite3_close(db);
        return;
    }

    std::string body = request.substr(body_pos + 4);
    json req;
    try {
        req = json::parse(body);
    } catch (...) {
        const char* resp =
            "HTTP/1.1 400 Bad Request\r\n"
            "Content-Type: application/json; charset=utf-8\r\n"
            "Content-Length: 57\r\n"
            "Connection: close\r\n"
            "\r\n"
            "{\"success\":false,\"code\":40002,\"message\":\"JSON解析失败\"}";
        write(fd, resp, strlen(resp));
        sqlite3_close(db);
        return;
    }

    /* ========== 3. 校验参数 ========== */
    int dish_id = -1;
    if (req.contains("dish_id") && req["dish_id"].is_number_integer()) {
        dish_id = req["dish_id"];
    } else if (req.contains("id") && req["id"].is_number_integer()) {
        dish_id = req["id"];
    } else {
        const char* resp =
            "HTTP/1.1 400 Bad Request\r\n"
            "Content-Type: application/json; charset=utf-8\r\n"
            "Content-Length: 63\r\n"
            "Connection: close\r\n"
            "\r\n"
            "{\"success\":false,\"code\":40003,\"message\":\"缺少 dish_id 或 id 参数\"}";
        write(fd, resp, strlen(resp));
        sqlite3_close(db);
        return;
    }

    /* ========== 4. 校验菜品归属 ========== */
    const char* owner_sql =
        "SELECT COUNT(*) FROM dishes WHERE id = ? AND merchant_id = ? AND deleted = 0";

    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db, owner_sql, -1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, dish_id);
    sqlite3_bind_int(stmt, 2, merchant_id);

    int owner_ok = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        owner_ok = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);

    if (!owner_ok) {
        const char* resp =
            "HTTP/1.1 403 Forbidden\r\n"
            "Content-Type: application/json; charset=utf-8\r\n"
            "Content-Length: 58\r\n"
            "Connection: close\r\n"
            "\r\n"
            "{\"success\":false,\"code\":40301,\"message\":\"无权操作该菜品\"}";
        write(fd, resp, strlen(resp));
        sqlite3_close(db);
        return;
    }

    /* ========== 5. 软删除菜品 ========== */
    const char* delete_sql = "UPDATE dishes SET deleted = 1 WHERE id = ? AND merchant_id = ?";

    sqlite3_prepare_v2(db, delete_sql, -1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, dish_id);
    sqlite3_bind_int(stmt, 2, merchant_id);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        const char* resp =
            "HTTP/1.1 500 Internal Server Error\r\n"
            "Content-Type: application/json; charset=utf-8\r\n"
            "Content-Length: 58\r\n"
            "Connection: close\r\n"
            "\r\n"
            "{\"success\":false,\"code\":50001,\"message\":\"数据库删除失败\"}";
        write(fd, resp, strlen(resp));
        sqlite3_finalize(stmt);
        sqlite3_close(db);
        return;
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);

    /* ========== 6. 成功返回 ========== */
    std::string response_body = "{\"success\":true,\"code\":0,\"message\":\"菜品删除成功\"}";
    std::string resp =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json; charset=utf-8\r\n"
        "Content-Length: " + std::to_string(response_body.length()) + "\r\n"
        "Connection: close\r\n"
        "\r\n" +
        response_body;

    write(fd, resp.c_str(), resp.size());

    fprintf(stderr, "[DELETE DISH] merchant_id=%d dish_id=%d\n",
            merchant_id, dish_id);
}