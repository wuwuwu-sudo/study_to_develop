#include "handle_user_and_merchant.h"
#include <sqlite3.h>
#include <uuid/uuid.h>
#include <ctime>

static const char* DB_PATH = "food_delivery.db";

/* ========== SQL 常量（只业务，不建表） ========== */

static const char* INSERT_USER = R"SQL(
INSERT INTO users (username, password) VALUES (?, ?);
)SQL";

static const char* INSERT_MERCHANT = R"SQL(
INSERT INTO merchants (username, password, shop_name, address)
VALUES (?, ?, ?, ?);
)SQL";


/* ========== 数据库工具 ========== */

static sqlite3* open_db() {
    sqlite3* db = nullptr;
    if (sqlite3_open(DB_PATH, &db) != SQLITE_OK) {
        LOG_ERROR(std::string("[DB] open failed: ") + sqlite3_errmsg(db));
        return nullptr;
    }
    return db;
}

/* ========== 业务函数 ========== */

bool save_user(const std::string& username, const std::string& password) {
    sqlite3* db = open_db();
    if (!db) return false;

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, INSERT_USER, -1, &stmt, nullptr) != SQLITE_OK) {
        sqlite3_close(db);
        return false;
    }

    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, password.c_str(), -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        LOG_ERROR(std::string("[save_user] failed: ") + sqlite3_errmsg(db));
    }

    bool ok = (rc == SQLITE_DONE);
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return ok;
}

bool save_merchant(
    const std::string& username,
    const std::string& password,
    const std::string& shop_name,
    const std::string& address
) {
    sqlite3* db = open_db();
    if (!db) return false;

    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db, INSERT_MERCHANT, -1, &stmt, nullptr);

    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, password.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, shop_name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, address.c_str(), -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        LOG_ERROR(std::string("[save_merchant] failed: ") + sqlite3_errmsg(db));
    }

    bool ok = (rc == SQLITE_DONE);
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return ok;
}

/* ========== Session / Cookie ========== */

std::string generate_session_id() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, 15);

    std::stringstream ss;
    for (int i = 0; i < 32; ++i)
        ss << std::hex << dis(gen);
    return ss.str();
}

bool get_cookie(const std::string& request,
                const std::string& name,
                std::string& value) {
    std::string cookie_header = "Cookie: ";
    auto pos = request.find(cookie_header);
    if (pos == std::string::npos) return false;

    auto end = request.find("\r\n", pos);
    std::string cookies = request.substr(pos + cookie_header.size(), end - pos - cookie_header.size());

    std::istringstream iss(cookies);
    std::string token;
    while (std::getline(iss, token, ';')) {
        while (!token.empty() && token.front() == ' ') token.erase(0, 1);
        auto eq = token.find('=');
        if (eq != std::string::npos && token.substr(0, eq) == name) {
            value = token.substr(eq + 1);
            return true;
        }
    }
    return false;
}

int get_id_by_session(const std::string& request, const std::string& field, const std::string& cookie_name) {
    std::string session_id;
    if (!get_cookie(request, cookie_name, session_id)) {
        return -1;
    }

    if (session_id.empty()) return -1;

    sqlite3* db = nullptr;
    if (sqlite3_open("food_delivery.db", &db) != SQLITE_OK) {
        return -1;
    }

    sqlite3_stmt* stmt = nullptr;
    std::string sql =
        "SELECT " + field +
        " FROM sessions WHERE session_id=? AND expire_time > ?";

    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        sqlite3_close(db);
        return -1;
    }

    sqlite3_bind_text(stmt, 1, session_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, time(nullptr));

    int id = -1;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        id = sqlite3_column_int(stmt, 0);
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return id;
}

int get_user_id_by_session(const std::string& request) {
    return get_id_by_session(request, "user_id", "user_session");
}

int get_merchant_id_by_session(const std::string& request) {
    return get_id_by_session(request, "merchant_id", "merchant_session");
}

/* ================= 工具函数实现 ================= */

bool starts_with(const char* str, const char* prefix) {
    return strncmp(str, prefix, strlen(prefix)) == 0;
}

bool ends_with(const char* str, const char* suffix) {
    size_t str_len = strlen(str);
    size_t suf_len = strlen(suffix);
    return str_len >= suf_len &&
           strcmp(str + str_len - suf_len, suffix) == 0;
}

const char* get_mime(const std::string& path) {
    if (ends_with(path.c_str(), ".html")) return "text/html; charset=utf-8";
    if (ends_with(path.c_str(), ".css"))  return "text/css; charset=utf-8";
    if (ends_with(path.c_str(), ".js"))   return "application/javascript; charset=utf-8";
    if (ends_with(path.c_str(), ".png"))  return "image/png";
    if (ends_with(path.c_str(), ".jpg"))  return "image/jpeg";
    if (ends_with(path.c_str(), ".jpeg")) return "image/jpeg";
    if (ends_with(path.c_str(), ".ico"))  return "image/x-icon";
    return "text/plain; charset=utf-8";
}

/* ================= HTTP 响应实现 ================= */

void send_404(int fd) {
    const char* body =
        "<!DOCTYPE html>"
        "<html><head><meta charset='utf-8'>"
        "<title>404 Not Found</title></head>"
        "<body><h1>404 Not Found</h1></body></html>";
    std::string header =
        "HTTP/1.1 404 Not Found\r\n"
        "Content-Type: text/html; charset=utf-8\r\n"
        "Content-Length: " + std::to_string(strlen(body)) + "\r\n"
        "\r\n";
    write(fd, header.data(), header.size());
    write(fd, body, strlen(body));
}

void send_json(int fd, const std::string& body) {
    std::string header =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json; charset=utf-8\r\n"
        "Content-Length: " + std::to_string(body.size()) + "\r\n"
        "\r\n";
    write(fd, header.data(), header.size());
    write(fd, body.data(), body.size());
}

void send_file(int fd, const char* path, const char* mime) {
    int file_fd = open(path, O_RDONLY);
    if (file_fd < 0) {
        send_404(fd);
        return;
    }

    struct stat st;
    fstat(file_fd, &st);

    std::string header =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: " + std::string(mime) + "\r\n"
        "Content-Length: " + std::to_string(st.st_size) + "\r\n"
        "\r\n";

    write(fd, header.data(), header.size());

    char buf[4096];
    ssize_t n;
    while ((n = read(file_fd, buf, sizeof(buf))) > 0) {
        write(fd, buf, n);
    }

    close(file_fd);
}

/* ================= 顾客 API（占位） ================= */

void handle_user_register(int fd, const std::string& request) {
    sqlite3* db = nullptr;
    if (sqlite3_open("food_delivery.db", &db) != SQLITE_OK) {
        const char* resp =
            "HTTP/1.1 500 Internal Server Error\r\n"
            "Content-Type: application/json; charset=utf-8\r\n"
            "Content-Length: 27\r\n"
            "Connection: close\r\n"
            "\r\n"
            "{\"error\":\"数据库打开失败\"}";
        write(fd, resp, strlen(resp));
        return;
    }

    /* ---------- 1. 提取 HTTP body ---------- */
    std::string body;
    auto pos = request.find("\r\n\r\n");
    if (pos == std::string::npos) {
        const char* resp =
            "HTTP/1.1 400 Bad Request\r\n"
            "Content-Type: application/json; charset=utf-8\r\n"
            "Content-Length: 24\r\n"
            "Connection: close\r\n"
            "\r\n"
            "{\"error\":\"非法请求\"}";
        write(fd, resp, strlen(resp));
        sqlite3_close(db);
        return;
    }
    body = request.substr(pos + 4);

    /* ---------- 2. 解析 JSON（UTF‑8 强制） ---------- */
    json req;
    try {
        req = json::parse(body);
    } catch (...) {
        const char* resp =
            "HTTP/1.1 400 Bad Request\r\n"
            "Content-Type: application/json; charset=utf-8\r\n"
            "Content-Length: 30\r\n"
            "Connection: close\r\n"
            "\r\n"
            "{\"error\":\"JSON 解析失败\"}";
        write(fd, resp, strlen(resp));
        sqlite3_close(db);
        return;
    }

    /* ---------- 3. 校验字段 ---------- */
    if (!req.contains("username") || !req["username"].is_string() ||
        !req.contains("password") || !req["password"].is_string()) {
        const char* resp =
            "HTTP/1.1 400 Bad Request\r\n"
            "Content-Type: application/json; charset=utf-8\r\n"
            "Content-Length: 30\r\n"
            "Connection: close\r\n"
            "\r\n"
            "{\"error\":\"参数不完整\"}";
        write(fd, resp, strlen(resp));
        sqlite3_close(db);
        return;
    }

    /* ---------- 4. 拷贝为具名 std::string（关键） ---------- */
    std::string username = req["username"];
    std::string password = req["password"];

    /* ---------- 5. 插入数据库 ---------- */
    const char* sql =
        "INSERT INTO users (username, password) VALUES (?, ?);";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        const char* resp =
            "HTTP/1.1 500 Internal Server Error\r\n"
            "Content-Type: application/json; charset=utf-8\r\n"
            "Content-Length: 27\r\n"
            "Connection: close\r\n"
            "\r\n"
            "{\"error\":\"SQL 准备失败\"}";
        write(fd, resp, strlen(resp));
        sqlite3_close(db);
        return;
    }

    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, password.c_str(), -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    /* ---------- 6. 返回响应 ---------- */
    std::string response_body;
    int status_code = 200;

    if (rc == SQLITE_DONE) {
        response_body = "{\"message\":\"注册成功\"}";
    } else if (rc == SQLITE_CONSTRAINT) {
        status_code = 409;
        response_body = "{\"message\":\"用户名已存在\"}";
    } else {
        status_code = 500;
        response_body = "{\"error\":\"服务器内部错误\"}";
    }

    std::string response =
        "HTTP/1.1 " + std::to_string(status_code) + " OK\r\n"
        "Content-Type: application/json; charset=utf-8\r\n"
        "Content-Length: " + std::to_string(response_body.size()) + "\r\n"
        "Connection: close\r\n"
        "\r\n" +
        response_body;

    ssize_t n = write(fd, response.c_str(), response.size());
    if (n < 0) {
        perror("write");
    } else if ((size_t)n != response.size()) {
        fprintf(stderr, "write partial: %zd/%zu\n", n, response.size());
    }

    sqlite3_close(db);
}

void handle_user_login(int fd, const std::string& request) {
    sqlite3* db = nullptr;
    if (sqlite3_open("food_delivery.db", &db) != SQLITE_OK) {
        const char* resp =
            "HTTP/1.1 500 Internal Server Error\r\n"
            "Content-Type: application/json; charset=utf-8\r\n"
            "Content-Length: 27\r\n"
            "Connection: close\r\n"
            "\r\n"
            "{\"error\":\"数据库打开失败\"}";
        write(fd, resp, strlen(resp));
        return;
    }

    /* ---------- 1. 提取 HTTP body ---------- */
    std::string body;
    auto pos = request.find("\r\n\r\n");
    if (pos == std::string::npos) {
        const char* resp =
            "HTTP/1.1 400 Bad Request\r\n"
            "Content-Type: application/json; charset=utf-8\r\n"
            "Content-Length: 24\r\n"
            "Connection: close\r\n"
            "\r\n"
            "{\"error\":\"非法请求\"}";
        write(fd, resp, strlen(resp));
        sqlite3_close(db);
        return;
    }
    body = request.substr(pos + 4);

    /* ---------- 2. 解析 JSON（UTF‑8 强制） ---------- */
    json req;
    try {
        req = json::parse(body);
    } catch (...) {
        const char* resp =
            "HTTP/1.1 400 Bad Request\r\n"
            "Content-Type: application/json; charset=utf-8\r\n"
            "Content-Length: 30\r\n"
            "Connection: close\r\n"
            "\r\n"
            "{\"error\":\"JSON 解析失败\"}";
        write(fd, resp, strlen(resp));
        sqlite3_close(db);
        return;
    }

    /* ---------- 3. 校验字段 ---------- */
    if (!req.contains("username") || !req["username"].is_string() ||
        !req.contains("password") || !req["password"].is_string()) {
        const char* resp =
            "HTTP/1.1 400 Bad Request\r\n"
            "Content-Type: application/json; charset=utf-8\r\n"
            "Content-Length: 30\r\n"
            "Connection: close\r\n"
            "\r\n"
            "{\"error\":\"参数不完整\"}";
        write(fd, resp, strlen(resp));
        sqlite3_close(db);
        return;
    }

    /* ---------- 4. 拷贝为具名 std::string（关键） ---------- */
    std::string username = req["username"];
    std::string password = req["password"];

    fprintf(stderr, "[LOGIN] try login: username=%s\n", username.c_str());

    /* ---------- 5. 查询用户 ---------- */
    const char* user_sql =
        "SELECT id, password FROM users WHERE username = ?;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, user_sql, -1, &stmt, nullptr) != SQLITE_OK) {
        const char* resp =
            "HTTP/1.1 500 Internal Server Error\r\n"
            "Content-Type: application/json; charset=utf-8\r\n"
            "Content-Length: 27\r\n"
            "Connection: close\r\n"
            "\r\n"
            "{\"error\":\"SQL 准备失败\"}";
        write(fd, resp, strlen(resp));
        sqlite3_close(db);
        return;
    }

    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);

    int user_id = -1;
    bool ok = false;

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        user_id = sqlite3_column_int(stmt, 0);
        std::string db_password =
            reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        ok = (password == db_password);
    }

    sqlite3_finalize(stmt);

    if (!ok || user_id < 0) {
        const char* resp_body = "{\"message\":\"用户名或密码错误\"}";
        std::string resp =
            "HTTP/1.1 401 Unauthorized\r\n"
            "Content-Type: application/json; charset=utf-8\r\n"
            "Content-Length: " + std::to_string(strlen(resp_body)) + "\r\n"
            "Connection: close\r\n"
            "\r\n" +
            resp_body;
        write(fd, resp.c_str(), resp.size());
        sqlite3_close(db);
        return;
    }

    /* ---------- 6. 创建 session ---------- */
    std::string session_id = generate_session_id();
    int64_t expire_time = time(nullptr) + 1800; // 30 min

    const char* sess_sql =
        "INSERT INTO sessions (session_id, user_id, merchant_id, expire_time) "
        "VALUES (?, ?, NULL, ?);";

    if (sqlite3_prepare_v2(db, sess_sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, session_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 2, user_id);
        sqlite3_bind_int64(stmt, 3, expire_time);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        fprintf(stderr, "[LOGIN] SUCCESS: username=%s session=%s\n",
                username.c_str(), session_id.c_str());
    }

    sqlite3_close(db);

    /* ---------- 7. 返回响应 ---------- */
    const char* resp_body = "{\"message\":\"登录成功\"}";
    std::string resp =
        "HTTP/1.1 200 OK\r\n"
        "Set-Cookie: user_session=" + session_id + "; Path=/; HttpOnly\r\n"
        "Content-Type: application/json; charset=utf-8\r\n"
        "Content-Length: " + std::to_string(strlen(resp_body)) + "\r\n"
        "Connection: close\r\n"
        "\r\n" +
        resp_body;

    write(fd, resp.c_str(), resp.size());
}

void handle_user_info(int fd, const std::string& request) {
    /* ---------- 1. 取 Cookie ---------- */
    std::string session_id;
    if (!get_cookie(request, "user_session", session_id)) {
        const char* body = "{\"error\":\"未登录\"}";
        std::string resp =
            "HTTP/1.1 401 Unauthorized\r\n"
            "Content-Type: application/json; charset=utf-8\r\n"
            "Content-Length: " + std::to_string(strlen(body)) + "\r\n"
            "Connection: close\r\n"
            "\r\n" +
            body;
        write(fd, resp.c_str(), resp.size());
        return;
    }

    sqlite3* db = nullptr;
    if (sqlite3_open("food_delivery.db", &db) != SQLITE_OK) {
        const char* body = "{\"error\":\"数据库打开失败\"}";
        std::string resp =
            "HTTP/1.1 500 Internal Server Error\r\n"
            "Content-Type: application/json; charset=utf-8\r\n"
            "Content-Length: " + std::to_string(strlen(body)) + "\r\n"
            "Connection: close\r\n"
            "\r\n" +
            body;
        write(fd, resp.c_str(), resp.size());
        return;
    }

    /* ---------- 2. 查 session ---------- */
    int user_id = -1;
    int64_t expire_time = 0;

    const char* sess_sql =
        "SELECT user_id, expire_time FROM sessions WHERE session_id = ?;";
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db, sess_sql, -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, session_id.c_str(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        user_id = sqlite3_column_int(stmt, 0);
        expire_time = sqlite3_column_int64(stmt, 1);
    }
    sqlite3_finalize(stmt);

    /* ---------- 3. session 无效或过期 ---------- */
    if (user_id < 0 || expire_time <= time(nullptr)) {
        const char* del_sql = "DELETE FROM sessions WHERE session_id = ?;";
        sqlite3_prepare_v2(db, del_sql, -1, &stmt, nullptr);
        sqlite3_bind_text(stmt, 1, session_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        sqlite3_close(db);

        fprintf(stderr, "[USER INFO] session expired or invalid: %s\n",
                session_id.c_str());

        const char* body = "{\"error\":\"登录已过期，请重新登录\"}";
        std::string resp =
            "HTTP/1.1 401 Unauthorized\r\n"
            "Set-Cookie: user_session=; Max-Age=0; Path=/; HttpOnly\r\n"
            "Content-Type: application/json; charset=utf-8\r\n"
            "Content-Length: " + std::to_string(strlen(body)) + "\r\n"
            "Connection: close\r\n"
            "\r\n" +
            body;
        write(fd, resp.c_str(), resp.size());
        return;
    }

    /* ---------- 4. 查用户信息（✅ 立刻拷贝） ---------- */
    const char* user_sql =
        "SELECT username FROM users WHERE id = ?;";
    sqlite3_prepare_v2(db, user_sql, -1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, user_id);

    std::string username;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        username = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    }
    sqlite3_finalize(stmt);

    sqlite3_close(db);

    /* ---------- 5. 用户不存在 ---------- */
    if (username.empty()) {
        const char* body = "{\"error\":\"用户不存在\"}";
        std::string resp =
            "HTTP/1.1 404 Not Found\r\n"
            "Content-Type: application/json; charset=utf-8\r\n"
            "Content-Length: " + std::to_string(strlen(body)) + "\r\n"
            "Connection: close\r\n"
            "\r\n" +
            body;
        write(fd, resp.c_str(), resp.size());
        return;
    }

    /* ---------- 6. 返回 JSON（✅ 100% 安全） ---------- */
    json j;
    j["user_id"] = user_id;
    j["username"] = username;
    j["status"] = "logged_in";

    std::string body = j.dump();
    std::string resp =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json; charset=utf-8\r\n"
        "Content-Length: " + std::to_string(body.size()) + "\r\n"
        "Connection: close\r\n"
        "\r\n" +
        body;

    write(fd, resp.c_str(), resp.size());

    fprintf(stderr, "[USER INFO] uid=%d username=%s\n",
            user_id, username.c_str());
}

void handle_get_shops(int fd, const std::string& request)
{
    (void)request;

    sqlite3* db = nullptr;
    
    if (sqlite3_open("food_delivery.db", &db) != SQLITE_OK) {
        const char* resp =
            "HTTP/1.1 500 Internal Server Error\r\n"
            "Content-Type: application/json; charset=utf-8\r\n"
            "Content-Length: 27\r\n"
            "Connection: close\r\n"
            "\r\n"
            "{\"error\":\"数据库打开失败\"}";
        write(fd, resp, strlen(resp));
        return;
    }

    const char* sql = 
        "SELECT id, shop_name, address, status FROM merchants WHERE status = 1 ORDER BY id";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        const char* resp =
            "HTTP/1.1 500 Internal Server Error\r\n"
            "Content-Type: application/json; charset=utf-8\r\n"
            "Content-Length: 27\r\n"
            "Connection: close\r\n"
            "\r\n"
            "{\"error\":\"SQL 准备失败\"}";
        write(fd, resp, strlen(resp));
        sqlite3_close(db);
        return;
    }

    std::ostringstream body;
    body << "[";

    bool first = true;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        if (!first) body << ",";
        first = false;

        int id = sqlite3_column_int(stmt, 0);
        const char* shop_name = (const char*)sqlite3_column_text(stmt, 1);
        const char* address = (const char*)sqlite3_column_text(stmt, 2);
        int status = sqlite3_column_int(stmt, 3);

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

        std::string safe_shop_name = shop_name ? shop_name : "";
        std::string safe_address = address ? address : "";

        body << "{"
             << "\"id\":" << id << ","
             << "\"shop_name\":\"" << escape_json(safe_shop_name) << "\","
             << "\"address\":\"" << escape_json(safe_address) << "\","
             << "\"status\":" << status
             << "}";
    }

    body << "]";

    sqlite3_finalize(stmt);
    sqlite3_close(db);

    std::string body_str = body.str();
    std::string resp =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json; charset=utf-8\r\n"
        "Content-Length: " + std::to_string(body_str.size()) + "\r\n"
        "Connection: close\r\n"
        "\r\n" +
        body_str;

    write(fd, resp.c_str(), resp.size());

    fprintf(stderr, "[GET SHOPS] 返回 %d 个营业中的商家\n", 
            (int)std::count(body_str.begin(), body_str.end(), '{'));
}

void handle_my_orders(int fd, const std::string& request)
{
    sqlite3* db = nullptr;
    
    /* ========== 1. 从 Cookie 取 session_id ========== */
    std::string session_id;
    auto ck_pos = request.find("Cookie:");
    if (ck_pos != std::string::npos) {
        auto sid_start = request.find("user_session=", ck_pos);
        if (sid_start != std::string::npos) {
            sid_start += 11;
            auto sid_end = request.find(';', sid_start);
            if (sid_end == std::string::npos)
                sid_end = request.find('\r', sid_start);
            session_id = request.substr(sid_start, sid_end - sid_start);
        }
    }

    if (session_id.empty()) {
        const char* resp =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/json; charset=utf-8\r\n"
            "Content-Length: 2\r\n"
            "Connection: close\r\n"
            "\r\n"
            "[]";
        write(fd, resp, strlen(resp));
        return;
    }

    if (sqlite3_open("food_delivery.db", &db) != SQLITE_OK) {
        const char* resp =
            "HTTP/1.1 500 Internal Server Error\r\n"
            "Content-Type: application/json; charset=utf-8\r\n"
            "Content-Length: 2\r\n"
            "Connection: close\r\n"
            "\r\n"
            "[]";
        write(fd, resp, strlen(resp));
        return;
    }

    /* ========== 2. 校验 session ========== */
    int user_id = -1;
    const char* sess_sql =
        "SELECT user_id FROM sessions WHERE session_id = ? AND expire_time > ?";

    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db, sess_sql, -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, session_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, time(nullptr));

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        user_id = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);

    if (user_id <= 0) {
        sqlite3_close(db);
        const char* resp =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/json; charset=utf-8\r\n"
            "Content-Length: 2\r\n"
            "Connection: close\r\n"
            "\r\n"
            "[]";
        write(fd, resp, strlen(resp));
        return;
    }

    /* ========== 3. 查询订单（没有 address 和 note） ========== */
    const char* orders_sql = 
        "SELECT id, merchant_id, total_price, status, created_at "
        "FROM orders WHERE user_id = ? ORDER BY created_at DESC";

    sqlite3_prepare_v2(db, orders_sql, -1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, user_id);

    std::ostringstream body;
    body << "[";

    bool first = true;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        if (!first) body << ",";
        first = false;

        int order_id = sqlite3_column_int(stmt, 0);
        int merchant_id = sqlite3_column_int(stmt, 1);
        int total_price = sqlite3_column_int(stmt, 2);
        int status = sqlite3_column_int(stmt, 3);
        const char* created_at = (const char*)sqlite3_column_text(stmt, 4);

        body << "{"
             << "\"id\":" << order_id << ","
             << "\"merchant_id\":" << merchant_id << ","
             << "\"total\":" << total_price << ","
             << "\"status\":" << status << ","
             << "\"created_at\":\"" << (created_at ? created_at : "") << "\","
             << "\"items\":[";

        // 查询订单明细
        const char* item_sql = 
            "SELECT dish_name, price, quantity FROM order_items WHERE order_id = ?";
        sqlite3_stmt* item_stmt = nullptr;
        sqlite3_prepare_v2(db, item_sql, -1, &item_stmt, nullptr);
        sqlite3_bind_int(item_stmt, 1, order_id);

        bool first_item = true;
        while (sqlite3_step(item_stmt) == SQLITE_ROW) {
            if (!first_item) body << ",";
            first_item = false;

            const char* dish_name = (const char*)sqlite3_column_text(item_stmt, 0);
            double price = sqlite3_column_double(item_stmt, 1);
            int quantity = sqlite3_column_int(item_stmt, 2);

            body << "{"
                 << "\"dish_name\":\"" << (dish_name ? dish_name : "") << "\","
                 << "\"price\":" << price << ","
                 << "\"qty\":" << quantity
                 << "}";
        }
        sqlite3_finalize(item_stmt);

        body << "]}";
    }

    body << "]";

    sqlite3_finalize(stmt);
    sqlite3_close(db);

    std::string body_str = body.str();
    std::string resp =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json; charset=utf-8\r\n"
        "Content-Length: " + std::to_string(body_str.size()) + "\r\n"
        "Connection: close\r\n"
        "\r\n" +
        body_str;

    write(fd, resp.c_str(), resp.size());

    fprintf(stderr, "[MY ORDERS] user_id=%d\n", user_id);
}

void handle_user_logout(int fd, const std::string& request) {
    std::string session_id;
    
    /* ---------- 1. 从 Cookie 中取 session_id ---------- */
    if (!get_cookie(request, "user_session", session_id)) {
        // 即使没有 session，也返回成功（清除客户端 Cookie）
        const char* body = "{\"message\":\"已登出\"}";
        std::string resp =
            "HTTP/1.1 200 OK\r\n"
            "Set-Cookie: user_session=; Max-Age=0; Path=/; HttpOnly\r\n"
            "Content-Type: application/json; charset=utf-8\r\n"
            "Content-Length: " + std::to_string(strlen(body)) + "\r\n"
            "Connection: close\r\n"
            "\r\n" +
            body;
        write(fd, resp.c_str(), resp.size());
        return;
    }

    /* ---------- 2. 删除数据库中的 session ---------- */
    sqlite3* db = nullptr;
    sqlite3_open("food_delivery.db", &db);

    const char* sql = "DELETE FROM sessions WHERE session_id = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, session_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        fprintf(stderr, "[LOGOUT] session destroyed: %s\n", session_id.c_str());
    }

    sqlite3_close(db);

    /* ---------- 3. 清除 Cookie 并返回成功 ---------- */
    const char* body = "{\"message\":\"已成功登出\"}";
    std::string resp =
        "HTTP/1.1 200 OK\r\n"
        "Set-Cookie: user_session=; Max-Age=0; Path=/; HttpOnly\r\n"
        "Content-Type: application/json; charset=utf-8\r\n"
        "Content-Length: " + std::to_string(strlen(body)) + "\r\n"
        "Connection: close\r\n"
        "\r\n" +
        body;

    write(fd, resp.c_str(), resp.size());
}

/* ================= 商家 API（占位） ================= */

void handle_merchant_register(int fd, const std::string& request) {
    sqlite3* db = nullptr;
    if (sqlite3_open("food_delivery.db", &db) != SQLITE_OK) {
        fprintf(stderr, "[MERCHANT REGISTER] sqlite3_open failed: %s\n",
                sqlite3_errmsg(db));
        sqlite3_close(db);
        return;
    }

    /* ---------- 1. 提取 HTTP body ---------- */
    std::string body;
    auto pos = request.find("\r\n\r\n");
    if (pos == std::string::npos) {
        const char* resp =
            "HTTP/1.1 400 Bad Request\r\n"
            "Content-Type: application/json; charset=utf-8\r\n"
            "Content-Length: 24\r\n"
            "Connection: close\r\n"
            "\r\n"
            "{\"error\":\"非法请求\"}";
        write(fd, resp, strlen(resp));
        sqlite3_close(db);
        return;
    }

    body = request.substr(pos + 4);

    /* ---------- 2. 解析 JSON ---------- */
    json req;
    try {
        req = json::parse(body);
    } catch (...) {
        const char* resp =
            "HTTP/1.1 400 Bad Request\r\n"
            "Content-Type: application/json; charset=utf-8\r\n"
            "Content-Length: 30\r\n"
            "Connection: close\r\n"
            "\r\n"
            "{\"error\":\"JSON 解析失败\"}";
        write(fd, resp, strlen(resp));
        sqlite3_close(db);
        return;
    }

    /* ---------- 3. 校验字段 ---------- */
    if (!req.contains("username") || !req["username"].is_string() ||
        !req.contains("password") || !req["password"].is_string() ||
        !req.contains("shop_name") || !req["shop_name"].is_string() ||
        !req.contains("address") || !req["address"].is_string()) { 
        const char* resp =
            "HTTP/1.1 400 Bad Request\r\n"
            "Content-Type: application/json; charset=utf-8\r\n"
            "Content-Length: 30\r\n"
            "Connection: close\r\n"
            "\r\n" 
            "{\"error\":\"参数不完整\"}";
        write(fd, resp, strlen(resp));
        sqlite3_close(db);
        return;
    }

    /* ---------- 4. 拷贝为具名 std::string（关键） ---------- */
    std::string username = req["username"];
    std::string password = req["password"];
    std::string shop_name = req["shop_name"];
    std::string address = req["address"];

    fprintf(stderr, "[MERCHANT REGISTER] username=%s shop=%s\n",
            username.c_str(), shop_name.c_str());

    /* ---------- 5. 插入数据库 ---------- */
    const char* sql =
        "INSERT INTO merchants (username, password, shop_name, address) "
        "VALUES (?, ?, ?, ?);";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        fprintf(stderr, "[MERCHANT REGISTER] SQL prepare failed: %s\n",
                sqlite3_errmsg(db));
        sqlite3_close(db);
        return;
    }

    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, password.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, shop_name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, address.c_str(), -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    std::string response;
    if (rc == SQLITE_DONE) {
        const char* body = "{\"message\":\"商家注册成功\"}";
        response =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/json; charset=utf-8\r\n"
            "Content-Length: " + std::to_string(strlen(body)) + "\r\n"
            "Connection: close\r\n"
            "\r\n" +
            body;
    }
    else if (rc == SQLITE_CONSTRAINT) {
        const char* body = "{\"message\":\"该商家账号已存在\"}";
        response =
            "HTTP/1.1 409 Conflict\r\n"
            "Content-Type: application/json; charset=utf-8\r\n"
            "Content-Length: " + std::to_string(strlen(body)) + "\r\n"
            "Connection: close\r\n"
            "\r\n" +
            body;
    }
    else {
        fprintf(stderr, "[MERCHANT REGISTER] DB ERROR: %s\n",
                sqlite3_errmsg(db));
        const char* body = "{\"message\":\"服务器内部错误\"}";
        response =
            "HTTP/1.1 500 Internal Server Error\r\n"
            "Content-Type: application/json; charset=utf-8\r\n"
            "Content-Length: " + std::to_string(strlen(body)) + "\r\n"
            "Connection: close\r\n"
            "\r\n" +
            body;
    }

    write(fd, response.c_str(), response.size());
    sqlite3_close(db);
}

void handle_merchant_login(int fd, const std::string& request) {
    sqlite3* db = nullptr;
    if (sqlite3_open("food_delivery.db", &db) != SQLITE_OK) {
        const char* resp =
            "HTTP/1.1 500 Internal Server Error\r\n"
            "Content-Type: application/json; charset=utf-8\r\n"
            "Content-Length: 27\r\n"
            "Connection: close\r\n"
            "\r\n"
            "{\"error\":\"数据库打开失败\"}";
        write(fd, resp, strlen(resp));
        return;
    }

    /* ---------- 1. 提取 HTTP body ---------- */
    std::string body;
    auto pos = request.find("\r\n\r\n");
    if (pos == std::string::npos) {
        const char* resp =
            "HTTP/1.1 400 Bad Request\r\n"
            "Content-Type: application/json; charset=utf-8\r\n"
            "Content-Length: 24\r\n"
            "Connection: close\r\n"
            "\r\n"
            "{\"error\":\"非法请求\"}";
        write(fd, resp, strlen(resp));
        sqlite3_close(db);
        return;
    }
    body = request.substr(pos + 4);

    /* ---------- 2. 解析 JSON ---------- */
    json req;
    try {
        req = json::parse(body);
    } catch (...) {
        const char* resp =
            "HTTP/1.1 400 Bad Request\r\n"
            "Content-Type: application/json; charset=utf-8\r\n"
            "Content-Length: 30\r\n"
            "Connection: close\r\n"
            "\r\n"
            "{\"error\":\"JSON 解析失败\"}";
        write(fd, resp, strlen(resp));
        sqlite3_close(db);
        return;
    }

    /* ---------- 3. 校验字段 ---------- */
    if (!req.contains("username") || !req["username"].is_string() ||
        !req.contains("password") || !req["password"].is_string()) {
        const char* resp =
            "HTTP/1.1 400 Bad Request\r\n"
            "Content-Type: application/json; charset=utf-8\r\n"
            "Content-Length: 30\r\n"
            "Connection: close\r\n"
            "\r\n"
            "{\"error\":\"参数不完整\"}";
        write(fd, resp, strlen(resp));
        sqlite3_close(db);
        return;
    }

    std::string username = req["username"];
    std::string password = req["password"];

    fprintf(stderr, "[MERCHANT LOGIN] try login: username=%s\n", username.c_str());

    /* ---------- 4. 查询商家 ---------- */
    const char* sql =
        "SELECT id, password FROM merchants WHERE username = ?;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        const char* resp =
            "HTTP/1.1 500 Internal Server Error\r\n"
            "Content-Type: application/json; charset=utf-8\r\n"
            "Content-Length: 27\r\n"
            "Connection: close\r\n"
            "\r\n"
            "{\"error\":\"SQL 准备失败\"}";
        write(fd, resp, strlen(resp));
        sqlite3_close(db);
        return;
    }

    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);

    int merchant_id = -1;
    bool ok = false;

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        merchant_id = sqlite3_column_int(stmt, 0);
        std::string db_password =
            reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        ok = (password == db_password);
    }

    sqlite3_finalize(stmt);

    /* ---------- 5. 登录失败 ---------- */
    if (!ok || merchant_id < 0) {
        const char* resp_body = "{\"message\":\"用户名或密码错误\"}";
        std::string resp =
            "HTTP/1.1 401 Unauthorized\r\n"
            "Content-Type: application/json; charset=utf-8\r\n"
            "Content-Length: " + std::to_string(strlen(resp_body)) + "\r\n"
            "Connection: close\r\n"
            "\r\n" +
            resp_body;
        write(fd, resp.c_str(), resp.size());
        sqlite3_close(db);
        return;
    }

    /* ---------- 6. 创建 Session ---------- */
    std::string session_id = generate_session_id();
    int64_t expire_time = time(nullptr) + 1800; // 30 分钟

    const char* sess_sql =
        "INSERT INTO sessions (session_id, user_id, merchant_id, expire_time) "
        "VALUES (?, NULL, ?, ?);";

    if (sqlite3_prepare_v2(db, sess_sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, session_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 2, merchant_id);
        sqlite3_bind_int64(stmt, 3, expire_time);

        if (sqlite3_step(stmt) != SQLITE_DONE) {
            fprintf(stderr, "[MERCHANT LOGIN] session insert error: %s\n",
                    sqlite3_errmsg(db));
        } else {
            fprintf(stderr,
                    "[MERCHANT LOGIN] SUCCESS: merchant_id=%d session=%s\n",
                    merchant_id, session_id.c_str());
        }

        sqlite3_finalize(stmt);
    }

    sqlite3_close(db);

    /* ---------- 7. 返回响应（使用 merchant_session） ---------- */
    const char* resp_body = "{\"message\":\"商家登录成功\"}";
    std::string resp =
        "HTTP/1.1 200 OK\r\n"
        "Set-Cookie: merchant_session=" + session_id + "; Path=/; HttpOnly\r\n"
        "Content-Type: application/json; charset=utf-8\r\n"
        "Content-Length: " + std::to_string(strlen(resp_body)) + "\r\n"
        "Connection: close\r\n"
        "\r\n" +
        resp_body;

    write(fd, resp.c_str(), resp.size());
}

void handle_merchant_info(int fd, const std::string& request) {
    /* ========== 1. 获取 merchant_id ========== */
    int merchant_id = get_merchant_id_by_session(request);
    if (merchant_id <= 0) {
        const char* body = "{\"error\":\"未登录\"}";
        std::string resp =
            "HTTP/1.1 401 Unauthorized\r\n"
            "Content-Type: application/json; charset=utf-8\r\n"
            "Content-Length: " + std::to_string(strlen(body)) + "\r\n"
            "Connection: close\r\n"
            "\r\n" +
            body;
        write(fd, resp.c_str(), resp.size());
        return;
    }

    sqlite3* db = nullptr;
    if (sqlite3_open("food_delivery.db", &db) != SQLITE_OK) {
        const char* body = "{\"error\":\"数据库打开失败\"}";
        std::string resp =
            "HTTP/1.1 500 Internal Server Error\r\n"
            "Content-Type: application/json; charset=utf-8\r\n"
            "Content-Length: " + std::to_string(strlen(body)) + "\r\n"
            "Connection: close\r\n"
            "\r\n" +
            body;
        write(fd, resp.c_str(), resp.size());
        return;
    }

    /* ========== 2. 查询商家信息 ========== */
    const char* merch_sql =
        "SELECT username, shop_name, address, status, created_at FROM merchants WHERE id = ?;";
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db, merch_sql, -1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, merchant_id);

    std::string username, shop_name, address, created_at;
    int status = 1;

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        username = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        shop_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        address = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        status = sqlite3_column_int(stmt, 3);
        created_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
    }
    sqlite3_finalize(stmt);

    if (username.empty()) {
        const char* body = "{\"error\":\"商家不存在\"}";
        std::string resp =
            "HTTP/1.1 404 Not Found\r\n"
            "Content-Type: application/json; charset=utf-8\r\n"
            "Content-Length: " + std::to_string(strlen(body)) + "\r\n"
            "Connection: close\r\n"
            "\r\n" +
            body;
        write(fd, resp.c_str(), resp.size());
        sqlite3_close(db);
        return;
    }

    sqlite3_close(db);

    /* ========== 3. 构造 JSON ========== */
    json j;
    j["merchant_id"] = merchant_id;
    j["username"] = username;
    j["shop_name"] = shop_name;
    j["address"] = address;
    j["status"] = status;
    j["created_at"] = created_at;

    std::string body = j.dump();
    std::string resp =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json; charset=utf-8\r\n"
        "Content-Length: " + std::to_string(body.size()) + "\r\n"
        "Connection: close\r\n"
        "\r\n" +
        body;

    write(fd, resp.c_str(), resp.size());

    fprintf(stderr, "[MERCHANT INFO] merchant_id=%d shop=%s status=%d\n",
            merchant_id, shop_name.c_str(), status);
}

void handle_merchant_status(int fd, const std::string& request)
{
    sqlite3* db = nullptr;
    
    fprintf(stderr, "[DEBUG] === handle_merchant_status 开始 ===\n");
    
    /* ========== 1. 获取 merchant_id ========== */
    int merchant_id = get_merchant_id_by_session(request);
    if (merchant_id <= 0) {
        fprintf(stderr, "[DEBUG] ❌ 未登录或 session 无效，返回 401\n");
        const char* resp =
            "HTTP/1.1 401 Unauthorized\r\n"
            "Content-Type: application/json; charset=utf-8\r\n"
            "Content-Length: 54\r\n"
            "Connection: close\r\n"
            "\r\n"
            "{\"success\":false,\"code\":40101,\"message\":\"未登录\"}";
        write(fd, resp, strlen(resp));
        return;
    }

    fprintf(stderr, "[DEBUG] ✅ 找到 merchant_id=%d\n", merchant_id);

    /* ========== 2. 打开数据库 ========== */
    if (sqlite3_open("food_delivery.db", &db) != SQLITE_OK) {
        fprintf(stderr, "[DEBUG] ❌ 数据库打开失败\n");
        const char* resp =
            "HTTP/1.1 500 Internal Server Error\r\n"
            "Content-Type: application/json; charset=utf-8\r\n"
            "Content-Length: 58\r\n"
            "Connection: close\r\n"
            "\r\n"
            "{\"success\":false,\"code\":50001,\"message\":\"数据库打开失败\"}";
        write(fd, resp, strlen(resp));
        return;
    }

    /* ========== 3. 解析 JSON Body ========== */
    size_t body_pos = request.find("\r\n\r\n");
    if (body_pos == std::string::npos) {
        fprintf(stderr, "[DEBUG] ❌ 找不到请求体\n");
        sqlite3_close(db);
        return;
    }

    std::string body = request.substr(body_pos + 4);
    fprintf(stderr, "[DEBUG] 请求体: %s\n", body.c_str());

    json req;
    try {
        req = json::parse(body);
    } catch (...) {
        fprintf(stderr, "[DEBUG] ❌ JSON 解析失败\n");
        const char* resp =
            "HTTP/1.1 400 Bad Request\r\n"
            "Content-Type: application/json; charset=utf-8\r\n"
            "Content-Length: 57\r\n"
            "Connection: close\r\n"
            "\r\n"
            "{\"success\":false,\"code\":40001,\"message\":\"JSON解析失败\"}";
        write(fd, resp, strlen(resp));
        sqlite3_close(db);
        return;
    }

    /* ========== 4. 校验参数 ========== */
    if (!req.contains("status") || !req["status"].is_number_integer()) {
        fprintf(stderr, "[DEBUG] ❌ 缺少 status 参数\n");
        const char* resp =
            "HTTP/1.1 400 Bad Request\r\n"
            "Content-Type: application/json; charset=utf-8\r\n"
            "Content-Length: 60\r\n"
            "Connection: close\r\n"
            "\r\n"
            "{\"success\":false,\"code\":40002,\"message\":\"缺少 status 参数\"}";
        write(fd, resp, strlen(resp));
        sqlite3_close(db);
        return;
    }

    int status = req["status"];
    fprintf(stderr, "[DEBUG] status=%d\n", status);

    if (status != 0 && status != 1) {
        fprintf(stderr, "[DEBUG] ❌ status 无效\n");
        const char* resp =
            "HTTP/1.1 400 Bad Request\r\n"
            "Content-Type: application/json; charset=utf-8\r\n"
            "Content-Length: 58\r\n"
            "Connection: close\r\n"
            "\r\n"
            "{\"success\":false,\"code\":40003,\"message\":\"status 必须为 0 或 1\"}";
        write(fd, resp, strlen(resp));
        sqlite3_close(db);
        return;
    }

    /* ========== 5. 更新商家状态 ========== */
    const char* update_sql = "UPDATE merchants SET status = ? WHERE id = ?";
    sqlite3_stmt* stmt = nullptr;
    
    sqlite3_prepare_v2(db, update_sql, -1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, status);
    sqlite3_bind_int(stmt, 2, merchant_id);

    fprintf(stderr, "[DEBUG] 执行更新: merchant_id=%d, status=%d\n", merchant_id, status);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        fprintf(stderr, "[DEBUG] ❌ 更新失败: %s\n", sqlite3_errmsg(db));
        const char* resp =
            "HTTP/1.1 500 Internal Server Error\r\n"
            "Content-Type: application/json; charset=utf-8\r\n"
            "Content-Length: 58\r\n"
            "Connection: close\r\n"
            "\r\n"
            "{\"success\":false,\"code\":50002,\"message\":\"状态更新失败\"}";
        write(fd, resp, strlen(resp));
        sqlite3_finalize(stmt);
        sqlite3_close(db);
        return;
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);

    fprintf(stderr, "[DEBUG] ✅ 状态更新成功\n");

    /* ========== 6. 成功返回 ========== */
    std::string response_body = "{\"success\":true,\"code\":0,\"message\":\"状态更新成功\"}";
    std::string resp =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json; charset=utf-8\r\n"
        "Content-Length: " + std::to_string(response_body.length()) + "\r\n"
        "Connection: close\r\n"
        "\r\n" +
        response_body;

    write(fd, resp.c_str(), resp.size());

    fprintf(stderr, "[MERCHANT STATUS] merchant_id=%d status=%d\n",
            merchant_id, status);
    fprintf(stderr, "[DEBUG] === handle_merchant_status 结束 ===\n");
}

void handle_merchant_logout(int fd, const std::string& request) {
    std::string session_id;

    /* ---------- 1. 从 Cookie 中取 merchant_session ---------- */
    if (!get_cookie(request, "merchant_session", session_id)) {
        // 即使没有 session，也返回成功（清除客户端 Cookie）
        const char* body = "{\"message\":\"已登出\"}";
        std::string resp =
            "HTTP/1.1 200 OK\r\n"
            "Set-Cookie: merchant_session=; Max-Age=0; Path=/; HttpOnly\r\n"
            "Content-Type: application/json; charset=utf-8\r\n"
            "Content-Length: " + std::to_string(strlen(body)) + "\r\n"
            "Connection: close\r\n"
            "\r\n" +
            body;
        write(fd, resp.c_str(), resp.size());
        return;
    }

    fprintf(stderr, "[MERCHANT LOGOUT] 商家登出: session_id=%s\n", session_id.c_str());

    /* ---------- 2. 删除数据库中的 session ---------- */
    sqlite3* db = nullptr;
    sqlite3_open("food_delivery.db", &db);

    const char* sql = "DELETE FROM sessions WHERE session_id = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, session_id.c_str(), -1, SQLITE_TRANSIENT);
        int rc = sqlite3_step(stmt);
        if (rc == SQLITE_DONE) {
            fprintf(stderr, "[MERCHANT LOGOUT] ✅ session 删除成功: %s\n", session_id.c_str());
        } else {
            fprintf(stderr, "[MERCHANT LOGOUT] ❌ session 删除失败: %s\n", sqlite3_errmsg(db));
        }
        sqlite3_finalize(stmt);
    }

    sqlite3_close(db);

    /* ---------- 3. 清除 Cookie 并返回成功 ---------- */
    const char* body = "{\"message\":\"商家已成功登出\"}";
    std::string resp =
        "HTTP/1.1 200 OK\r\n"
        "Set-Cookie: merchant_session=; Max-Age=0; Path=/; HttpOnly\r\n"
        "Content-Type: application/json; charset=utf-8\r\n"
        "Content-Length: " + std::to_string(strlen(body)) + "\r\n"
        "Connection: close\r\n"
        "\r\n" +
        body;

    write(fd, resp.c_str(), resp.size());

    fprintf(stderr, "[MERCHANT LOGOUT] 商家登出成功\n");
}

void handle_merchant_orders(int fd, const std::string& request)
{
    sqlite3* db = nullptr;
    
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
        return;
    }

    /* ========== 2. 打开数据库 ========== */
    if (sqlite3_open("food_delivery.db", &db) != SQLITE_OK) {
        const char* resp =
            "HTTP/1.1 500 Internal Server Error\r\n"
            "Content-Type: application/json; charset=utf-8\r\n"
            "Content-Length: 58\r\n"
            "Connection: close\r\n"
            "\r\n"
            "{\"success\":false,\"code\":50001,\"message\":\"数据库打开失败\"}";
        write(fd, resp, strlen(resp));
        return;
    }

    /* ========== 3. 查询该商家的订单 ========== */
    const char* orders_sql = 
        "SELECT o.id, o.user_id, o.total_price, o.status, o.created_at, "
        "u.username as customer_name "
        "FROM orders o "
        "LEFT JOIN users u ON o.user_id = u.id "
        "WHERE o.merchant_id = ? "
        "ORDER BY o.created_at DESC";

    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db, orders_sql, -1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, merchant_id);

    std::ostringstream body;
    body << "[";

    bool first = true;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        if (!first) body << ",";
        first = false;

        int order_id = sqlite3_column_int(stmt, 0);
        int user_id = sqlite3_column_int(stmt, 1);
        int total_price = sqlite3_column_int(stmt, 2);
        int status = sqlite3_column_int(stmt, 3);
        const char* created_at = (const char*)sqlite3_column_text(stmt, 4);
        const char* customer_name = (const char*)sqlite3_column_text(stmt, 5);

        // 查询该订单的明细
        const char* item_sql = 
            "SELECT dish_name, price, quantity FROM order_items WHERE order_id = ?";
        sqlite3_stmt* item_stmt = nullptr;
        sqlite3_prepare_v2(db, item_sql, -1, &item_stmt, nullptr);
        sqlite3_bind_int(item_stmt, 1, order_id);

        // 构建订单 JSON
        body << "{"
             << "\"id\":" << order_id << ","
             << "\"customer_id\":" << user_id << ","
             << "\"customer_name\":\"" << (customer_name ? customer_name : "顾客" + std::to_string(user_id)) << "\","
             << "\"total\":" << total_price << ","
             << "\"status\":" << status << ","
             << "\"created_at\":\"" << (created_at ? created_at : "") << "\","
             << "\"items\":[";

        bool first_item = true;
        while (sqlite3_step(item_stmt) == SQLITE_ROW) {
            if (!first_item) body << ",";
            first_item = false;

            const char* dish_name = (const char*)sqlite3_column_text(item_stmt, 0);
            double price = sqlite3_column_double(item_stmt, 1);
            int quantity = sqlite3_column_int(item_stmt, 2);

            body << "{"
                 << "\"dish_name\":\"" << (dish_name ? dish_name : "") << "\","
                 << "\"price\":" << price << ","
                 << "\"qty\":" << quantity
                 << "}";
        }
        sqlite3_finalize(item_stmt);

        body << "]}";
    }

    body << "]";

    sqlite3_finalize(stmt);
    sqlite3_close(db);

    /* ========== 4. 返回响应 ========== */
    std::string body_str = body.str();
    std::string resp =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json; charset=utf-8\r\n"
        "Content-Length: " + std::to_string(body_str.size()) + "\r\n"
        "Connection: close\r\n"
        "\r\n" +
        body_str;

    write(fd, resp.c_str(), resp.size());

    fprintf(stderr, "[MERCHANT ORDERS] merchant_id=%d, orders_count=%d\n", 
            merchant_id, 
            (int)std::count(body_str.begin(), body_str.end(), '{'));
}

/* ================= 订单 API（占位） ================= */

void handle_order_submit(int fd, const std::string& request)
{
    sqlite3* db = nullptr;
    
    /* ========== 1. 获取 user_id（使用 customer_session） ========== */
    int user_id = get_user_id_by_session(request);
    if (user_id <= 0) {
        const char* resp =
            "HTTP/1.1 401 Unauthorized\r\n"
            "Content-Type: application/json; charset=utf-8\r\n"
            "Content-Length: 54\r\n"
            "Connection: close\r\n"
            "\r\n"
            "{\"success\":false,\"code\":40101,\"message\":\"未登录\"}";
        write(fd, resp, strlen(resp));
        return;
    }

    /* ========== 2. 打开数据库 ========== */
    if (sqlite3_open("food_delivery.db", &db) != SQLITE_OK) {
        const char* resp =
            "HTTP/1.1 500 Internal Server Error\r\n"
            "Content-Type: application/json; charset=utf-8\r\n"
            "Content-Length: 58\r\n"
            "Connection: close\r\n"
            "\r\n"
            "{\"success\":false,\"code\":50001,\"message\":\"数据库打开失败\"}";
        write(fd, resp, strlen(resp));
        return;
    }

    /* ========== 3. 解析 JSON Body ========== */
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

    /* ========== 4. 校验参数 ========== */
    if (!req.contains("merchant_id") || !req["merchant_id"].is_number_integer() ||
        !req.contains("items") || !req["items"].is_array()) {
        const char* resp =
            "HTTP/1.1 400 Bad Request\r\n"
            "Content-Type: application/json; charset=utf-8\r\n"
            "Content-Length: 57\r\n"
            "Connection: close\r\n"
            "\r\n"
            "{\"success\":false,\"code\":40003,\"message\":\"参数不完整\"}";
        write(fd, resp, strlen(resp));
        sqlite3_close(db);
        return;
    }

    int merchant_id = req["merchant_id"];
    auto items = req["items"];

    if (items.empty()) {
        const char* resp =
            "HTTP/1.1 400 Bad Request\r\n"
            "Content-Type: application/json; charset=utf-8\r\n"
            "Content-Length: 57\r\n"
            "Connection: close\r\n"
            "\r\n"
            "{\"success\":false,\"code\":40004,\"message\":\"购物车为空\"}";
        write(fd, resp, strlen(resp));
        sqlite3_close(db);
        return;
    }

    /* ========== 5. 计算总价 ========== */
    int total_price = 0;
    
    for (const auto& item : items) {
        if (!item.contains("dish_id") || !item["dish_id"].is_number_integer() ||
            !item.contains("qty") || !item["qty"].is_number_integer()) {
            continue;
        }
        
        int dish_id = item["dish_id"];
        int qty = item["qty"];
        
        const char* price_sql = 
            "SELECT price FROM dishes WHERE id = ? AND merchant_id = ? AND deleted = 0 AND available = 1";
        sqlite3_stmt* price_stmt = nullptr;
        sqlite3_prepare_v2(db, price_sql, -1, &price_stmt, nullptr);
        sqlite3_bind_int(price_stmt, 1, dish_id);
        sqlite3_bind_int(price_stmt, 2, merchant_id);
        
        if (sqlite3_step(price_stmt) == SQLITE_ROW) {
            double price = sqlite3_column_double(price_stmt, 0);
            total_price += static_cast<int>(price * qty);
        } else {
            sqlite3_finalize(price_stmt);
            const char* resp =
                "HTTP/1.1 400 Bad Request\r\n"
                "Content-Type: application/json; charset=utf-8\r\n"
                "Content-Length: 57\r\n"
                "Connection: close\r\n"
                "\r\n"
                "{\"success\":false,\"code\":40005,\"message\":\"菜品不存在或已下架\"}";
            write(fd, resp, strlen(resp));
            sqlite3_close(db);
            return;
        }
        sqlite3_finalize(price_stmt);
    }

    /* ========== 6. 插入订单 ========== */
    const char* order_sql = 
        "INSERT INTO orders (user_id, merchant_id, total_price, status) "
        "VALUES (?, ?, ?, 0)";

    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db, order_sql, -1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, user_id);
    sqlite3_bind_int(stmt, 2, merchant_id);
    sqlite3_bind_int(stmt, 3, total_price);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        const char* resp =
            "HTTP/1.1 500 Internal Server Error\r\n"
            "Content-Type: application/json; charset=utf-8\r\n"
            "Content-Length: 58\r\n"
            "Connection: close\r\n"
            "\r\n"
            "{\"success\":false,\"code\":50002,\"message\":\"订单创建失败\"}";
        write(fd, resp, strlen(resp));
        sqlite3_finalize(stmt);
        sqlite3_close(db);
        return;
    }

    int order_id = static_cast<int>(sqlite3_last_insert_rowid(db));
    sqlite3_finalize(stmt);

    /* ========== 7. 插入订单明细 ========== */
    const char* item_sql = 
        "INSERT INTO order_items (order_id, dish_id, dish_name, price, quantity) "
        "VALUES (?, ?, (SELECT name FROM dishes WHERE id = ?), (SELECT price FROM dishes WHERE id = ?), ?)";

    for (const auto& item : items) {
        if (!item.contains("dish_id") || !item["dish_id"].is_number_integer() ||
            !item.contains("qty") || !item["qty"].is_number_integer()) {
            continue;
        }
        
        int dish_id = item["dish_id"];
        int qty = item["qty"];
        
        sqlite3_prepare_v2(db, item_sql, -1, &stmt, nullptr);
        sqlite3_bind_int(stmt, 1, order_id);
        sqlite3_bind_int(stmt, 2, dish_id);
        sqlite3_bind_int(stmt, 3, dish_id);
        sqlite3_bind_int(stmt, 4, dish_id);
        sqlite3_bind_int(stmt, 5, qty);
        
        if (sqlite3_step(stmt) != SQLITE_DONE) {
            fprintf(stderr, "[ORDER SUBMIT] 插入订单明细失败: dish_id=%d\n", dish_id);
        }
        sqlite3_finalize(stmt);
    }

    sqlite3_close(db);

    /* ========== 8. 成功返回 ========== */
    std::string response_body = 
        "{\"success\":true,\"code\":0,\"message\":\"下单成功\",\"order_id\":" + 
        std::to_string(order_id) + "}";
    std::string resp =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json; charset=utf-8\r\n"
        "Content-Length: " + std::to_string(response_body.length()) + "\r\n"
        "Connection: close\r\n"
        "\r\n" +
        response_body;

    write(fd, resp.c_str(), resp.size());

    fprintf(stderr, "[ORDER SUBMIT] user_id=%d merchant_id=%d order_id=%d total=%d\n",
            user_id, merchant_id, order_id, total_price);
}

void handle_update_order_status(int fd, const std::string& request)
{
    sqlite3* db = nullptr;
    
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
        return;
    }

    /* ========== 3. 校验参数 ========== */
    if (!req.contains("order_id") || !req["order_id"].is_number_integer() ||
        !req.contains("status") || !req["status"].is_number_integer()) {
        const char* resp =
            "HTTP/1.1 400 Bad Request\r\n"
            "Content-Type: application/json; charset=utf-8\r\n"
            "Content-Length: 57\r\n"
            "Connection: close\r\n"
            "\r\n"
            "{\"success\":false,\"code\":40003,\"message\":\"参数不完整\"}";
        write(fd, resp, strlen(resp));
        return;
    }

    int order_id = req["order_id"];
    int status = req["status"];

    /* ========== 4. 校验状态值 ========== */
    if (status != -1 && status != 1 && status != 2) {
        const char* resp =
            "HTTP/1.1 400 Bad Request\r\n"
            "Content-Type: application/json; charset=utf-8\r\n"
            "Content-Length: 58\r\n"
            "Connection: close\r\n"
            "\r\n"
            "{\"success\":false,\"code\":40004,\"message\":\"状态值无效\"}";
        write(fd, resp, strlen(resp));
        return;
    }

    /* ========== 5. 打开数据库 ========== */
    if (sqlite3_open("food_delivery.db", &db) != SQLITE_OK) {
        const char* resp =
            "HTTP/1.1 500 Internal Server Error\r\n"
            "Content-Type: application/json; charset=utf-8\r\n"
            "Content-Length: 58\r\n"
            "Connection: close\r\n"
            "\r\n"
            "{\"success\":false,\"code\":50001,\"message\":\"数据库打开失败\"}";
        write(fd, resp, strlen(resp));
        return;
    }

    /* ========== 6. 验证订单属于该商家 ========== */
    const char* check_sql = 
        "SELECT COUNT(*) FROM orders WHERE id = ? AND merchant_id = ?";
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db, check_sql, -1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, order_id);
    sqlite3_bind_int(stmt, 2, merchant_id);

    int count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);

    if (count == 0) {
        const char* resp =
            "HTTP/1.1 403 Forbidden\r\n"
            "Content-Type: application/json; charset=utf-8\r\n"
            "Content-Length: 58\r\n"
            "Connection: close\r\n"
            "\r\n"
            "{\"success\":false,\"code\":40301,\"message\":\"无权操作该订单\"}";
        write(fd, resp, strlen(resp));
        sqlite3_close(db);
        return;
    }

    /* ========== 7. 更新订单状态 ========== */
    const char* update_sql = "UPDATE orders SET status = ? WHERE id = ?";
    sqlite3_prepare_v2(db, update_sql, -1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, status);
    sqlite3_bind_int(stmt, 2, order_id);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        const char* resp =
            "HTTP/1.1 500 Internal Server Error\r\n"
            "Content-Type: application/json; charset=utf-8\r\n"
            "Content-Length: 58\r\n"
            "Connection: close\r\n"
            "\r\n"
            "{\"success\":false,\"code\":50002,\"message\":\"订单状态更新失败\"}";
        write(fd, resp, strlen(resp));
        sqlite3_finalize(stmt);
        sqlite3_close(db);
        return;
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);

    /* ========== 8. 成功返回 ========== */
    std::string response_body = "{\"success\":true,\"code\":0,\"message\":\"订单状态更新成功\"}";
    std::string resp =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json; charset=utf-8\r\n"
        "Content-Length: " + std::to_string(response_body.length()) + "\r\n"
        "Connection: close\r\n"
        "\r\n" +
        response_body;

    write(fd, resp.c_str(), resp.size());

    fprintf(stderr, "[UPDATE ORDER STATUS] merchant_id=%d order_id=%d status=%d\n",
            merchant_id, order_id, status);
}