#include "presentation/http/http_parser.h"

#include <sstream>
#include <string_view>
#include <utility>
#include <vector>

namespace presentation::http {

namespace {

// 从视图当前位置取一行（去掉行尾 \r\n 或 \n），返回该行视图并前进 pos。
// 避免 parse_one 原来 `buffer.substr(0, header_end)` 对整段头部的一次拷贝。
std::string_view next_line(std::string_view view, std::size_t& pos) {
    if (pos >= view.size()) {
        return {};
    }
    const std::size_t start = pos;
    const std::size_t nl = view.find('\n', start);
    if (nl == std::string_view::npos) {
        pos = view.size();
        std::string_view line = view.substr(start);
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        return line;
    }
    pos = nl + 1;
    std::string_view line = view.substr(start, nl - start);
    if (!line.empty() && line.back() == '\r') {
        line.remove_suffix(1);
    }
    return line;
}

}  // namespace

bool HttpParser::parse(const std::string& raw_request, HttpRequest& request) {
    std::istringstream stream(raw_request);
    std::string line;

    // 请求行
    if (!std::getline(stream, line)) {
        return false;
    }
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }

    std::istringstream request_line(line);
    std::string method_str;
    std::string path_with_query;
    std::string version;
    request_line >> method_str >> path_with_query >> version;

    if (method_str == "GET") request.method = HttpMethod::GET;
    else if (method_str == "POST") request.method = HttpMethod::POST;
    else if (method_str == "PUT") request.method = HttpMethod::PUT;
    else if (method_str == "DELETE") request.method = HttpMethod::DELETE;
    else return false;

    std::size_t query_pos = path_with_query.find('?');
    if (query_pos != std::string::npos) {
        request.path = path_with_query.substr(0, query_pos);
        std::istringstream query_stream(path_with_query.substr(query_pos + 1));
        std::string pair;
        while (std::getline(query_stream, pair, '&')) {
            std::size_t eq_pos = pair.find('=');
            if (eq_pos != std::string::npos) {
                request.query_params[pair.substr(0, eq_pos)] = pair.substr(eq_pos + 1);
            }
        }
    } else {
        request.path = path_with_query;
    }
    request.version = version;

    // 请求头：遇到空行结束（先移除 '\r' 再判断）
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) {
            break;  // 头部结束
        }
        std::size_t colon_pos = line.find(':');
        if (colon_pos != std::string::npos) {
            std::string key = line.substr(0, colon_pos);
            std::string value = line.substr(colon_pos + 1);
            while (!value.empty() && value.front() == ' ') {
                value.erase(0, 1);
            }
            request.headers[key] = value;
        }
    }

    // 请求体：优先按 Content-Length 精确截取
    auto it = request.headers.find("Content-Length");
    if (it != request.headers.end()) {
        std::size_t body_len = 0;
        try {
            body_len = std::stoul(it->second);
        } catch (...) {
            return false;  // 非法 Content-Length
        }
        if (body_len > 0) {
            std::string body(body_len, '\0');
            stream.read(&body[0], static_cast<std::streamsize>(body_len));
            if (stream.gcount() != static_cast<std::streamsize>(body_len)) {
                return false;  // body 长度不足
            }
            request.body = std::move(body);
        }
    } else {
        // 无 Content-Length：读取剩余全部（仅适用于短连接）
        std::ostringstream body;
        body << stream.rdbuf();
        request.body = body.str();
    }

    return true;
}

int HttpParser::parse_one(const std::string& buffer, std::size_t& consumed,
                          HttpRequest& request) {
    consumed = 0;

    // 1. 定位头部结束位置（空行），支持 \r\n\r\n 与 \n\n 两种行尾
    std::size_t header_end = buffer.find("\r\n\r\n");
    std::size_t header_sep_len = 4;
    if (header_end == std::string::npos) {
        header_end = buffer.find("\n\n");
        header_sep_len = 2;
    }
    if (header_end == std::string::npos) {
        return 0;  // 头部未收全
    }

    // 2. 解析请求行与头部（仅取头部块；string_view 零拷贝，避免整段头部 substr 拷贝）
    const std::string_view header_block(buffer.data(), header_end);
    std::size_t pos = 0;
    const std::string_view first_line = next_line(header_block, pos);
    if (first_line.empty()) {
        return -1;
    }
    // 请求行手工解析（string_view 零拷贝，消除 istringstream/locale/ios 开销）：
    // 语义对齐原 istringstream operator>> —— 跳过任意空白、按空白切 method/path/version。
    const auto is_ws = [](char c) {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n';
    };
    std::size_t rl_pos = 0;
    const auto skip_ws = [&]() {
        while (rl_pos < first_line.size() && is_ws(first_line[rl_pos])) {
            ++rl_pos;
        }
    };
    const auto next_token = [&]() -> std::string_view {
        skip_ws();
        const std::size_t start = rl_pos;
        while (rl_pos < first_line.size() && !is_ws(first_line[rl_pos])) {
            ++rl_pos;
        }
        return first_line.substr(start, rl_pos - start);
    };
    const std::string_view method_sv = next_token();
    const std::string_view path_sv = next_token();
    const std::string_view version_sv = next_token();
    if (method_sv.empty() || path_sv.empty() || version_sv.empty()) {
        return -1;
    }

    if (method_sv == "GET") request.method = HttpMethod::GET;
    else if (method_sv == "POST") request.method = HttpMethod::POST;
    else if (method_sv == "PUT") request.method = HttpMethod::PUT;
    else if (method_sv == "DELETE") request.method = HttpMethod::DELETE;
    else return -1;

    const std::size_t query_pos = path_sv.find('?');
    if (query_pos != std::string_view::npos) {
        request.path = std::string(path_sv.substr(0, query_pos));
        // query 切分（string_view；语义对齐原 getline(stream, pair, '&')）
        const std::string_view query_sv = path_sv.substr(query_pos + 1);
        std::size_t qpos = 0;
        for (;;) {
            const std::size_t amp = query_sv.find('&', qpos);
            const std::string_view pair =
                (amp == std::string_view::npos)
                    ? query_sv.substr(qpos)
                    : query_sv.substr(qpos, amp - qpos);
            const std::size_t eq = pair.find('=');
            if (eq != std::string_view::npos) {
                request.query_params[std::string(pair.substr(0, eq))] =
                    std::string(pair.substr(eq + 1));
            }
            if (amp == std::string_view::npos) {
                break;
            }
            qpos = amp + 1;
        }
    } else {
        request.path = std::string(path_sv);
    }
    request.version = std::string(version_sv);

    // 头部逐行解析：从视图取行（无整段拷贝），再拷贝出 key/value 存入 map
    while (pos < header_block.size()) {
        const std::string_view hline = next_line(header_block, pos);
        if (hline.empty()) {
            break;  // 空行 = 头部结束
        }
        const std::size_t colon_pos = hline.find(':');
        if (colon_pos != std::string_view::npos) {
            std::string key(hline.substr(0, colon_pos));
            std::string value(hline.substr(colon_pos + 1));
            // 去除前导空格（与原来逐次 erase(0,1) 语义一致，避免 O(n²) 移位）
            const std::size_t vstart = value.find_first_not_of(' ');
            if (vstart == std::string::npos) {
                value.clear();  // 值全为空格 → 原实现会清空
            } else if (vstart > 0) {
                value.erase(0, vstart);
            }
            request.headers[std::move(key)] = std::move(value);
        }
    }

    // 3. 请求体：仅按 Content-Length 精确截取（无 Content-Length 视为无 body，
    //    避免在长连接上把下一个请求误当 body）
    std::size_t body_start = header_end + header_sep_len;
    std::size_t body_len = 0;
    auto it = request.headers.find("Content-Length");
    if (it != request.headers.end()) {
        try {
            body_len = std::stoul(it->second);
        } catch (...) {
            return -1;  // 非法 Content-Length
        }
    }
    if (buffer.size() < body_start + body_len) {
        return 0;  // body 未收全
    }

    request.body = buffer.substr(body_start, body_len);
    consumed = body_start + body_len;
    return 1;
}

}  // namespace presentation::http
