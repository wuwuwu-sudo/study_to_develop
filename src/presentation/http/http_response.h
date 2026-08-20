#pragma once

#include <string>
#include <unordered_map>

namespace presentation::http {

class HttpResponse {
public:
    int status_code = 200;
    std::unordered_map<std::string, std::string> headers;
    std::string body;

    void set_status(int status_code);
    void set_content_type(const std::string& content_type);
    void set_header(const std::string& name, const std::string& value);
    // 按值接收 + 移动：调用方传临时字符串（如 JSON dump 结果、缓存命中值）时
    // 不再多拷贝一次响应体（热路径 /api/shops、/api/dishes 直接受益）。
    void set_json(std::string json_body);
    void set_text(std::string text_body);
    void set_body(std::string body_text);
    void send_file(const std::string& path, const std::string& content_type);
    void redirect(const std::string& location);
    std::string serialize() const;
    // 复用外部缓冲的序列化：把响应序列化写入 out（保留容量，减少每请求分配）。
    void serialize_into(std::string& out) const;
    // 只序列化响应头（不含 body，以空行结束），供 writev 拆分发送省掉 body 拷贝。
    void serialize_header_into(std::string& header) const;
};

}  // namespace presentation::http
