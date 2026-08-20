#include "presentation/http/http_request.h"

#include <cctype>

namespace {

// 大小写不敏感比较（HTTP 头名不区分大小写）。逐字符比较，
// 避免原实现对每个 header 键都分配一份小写拷贝 + transform。
bool iequals(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

}  // namespace

namespace presentation::http {

const std::string& HttpRequest::get_path() const {
    return path;
}

std::string HttpRequest::header(const std::string& name) const {
    auto it = headers.find(name);
    if (it != headers.end()) {
        return it->second;
    }

    // 大小写不敏感回退：直接逐字符比较，不再为每个键创建小写副本
    for (const auto& [key, value] : headers) {
        if (iequals(key, name)) {
            return value;
        }
    }

    return "";
}

std::string HttpRequest::query(const std::string& name) const {
    auto it = query_params.find(name);
    return it == query_params.end() ? std::string() : it->second;
}

}  // namespace presentation::http
