#include "presentation/http/http_response.h"

#include <fstream>
#include <sstream>
#include <utility>

namespace presentation::http {

static const char* status_message(int code);

void HttpResponse::set_status(int status) {
    status_code = status;
}

void HttpResponse::set_content_type(const std::string& content_type) {
    headers["Content-Type"] = content_type;
}

void HttpResponse::set_header(const std::string& name, const std::string& value) {
    headers[name] = value;
}

void HttpResponse::set_json(std::string json_body) {
    set_content_type("application/json");
    body = std::move(json_body);
}

void HttpResponse::set_text(std::string text_body) {
    set_content_type("text/plain");
    body = std::move(text_body);
}

void HttpResponse::set_body(std::string body_text) {
    body = std::move(body_text);
}

void HttpResponse::send_file(const std::string& path, const std::string& content_type) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        set_status(404);
        set_body("Not Found");
        return;
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();
    set_status(200);
    set_content_type(content_type);
    body = buffer.str();
}

void HttpResponse::redirect(const std::string& location) {
    set_status(302);
    headers["Location"] = location;
    set_body("");
}

std::string HttpResponse::serialize() const {
    std::string out;
    serialize_into(out);
    return out;
}

void HttpResponse::serialize_into(std::string& out) const {
    serialize_header_into(out);  // 构建头部（内部 clear 并复用容量）
    out += body;
}

void HttpResponse::serialize_header_into(std::string& header) const {
    // 只序列化响应头（状态行 + 各头 + Content-Length + 空行），不含 body，
    // 供 writev 拆分发送以省掉 body 的整串拷贝。
    header.clear();
    std::size_t est = 64;  // 状态行 + 常量头 + 空行
    for (const auto& [key, value] : headers) {
        est += key.size() + value.size() + 4;  // "key: value\r\n"
    }
    header.reserve(est);

    header += "HTTP/1.1 ";
    header += std::to_string(status_code);
    header += ' ';
    header += status_message(status_code);
    header += "\r\n";

    for (const auto& [key, value] : headers) {
        // Content-Length 由本函数统一写入，跳过外部设置以免重复
        if (key == "Content-Length") {
            continue;
        }
        header += key;
        header += ": ";
        header += value;
        header += "\r\n";
    }
    header += "Content-Length: ";
    header += std::to_string(body.size());
    header += "\r\n\r\n";
}

static const char* status_message(int code) {
    switch (code) {
        case 200: return "OK";
        case 201: return "Created";
        case 204: return "No Content";
        case 302: return "Found";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 409: return "Conflict";
        case 429: return "Too Many Requests";
        case 500: return "Internal Server Error";
        case 501: return "Not Implemented";
        default: return "Unknown";
    }
}

}  // namespace presentation::http
