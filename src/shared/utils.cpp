#include "utils.h"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <random>
#include <sstream>

#include <openssl/sha.h>

namespace {

// 辅助函数：计算 SHA-256 并以十六进制字符串返回（仅本文件可见）
std::string sha256_hex(const std::string& input) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(input.data()),
           input.size(), hash);

    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (unsigned char c : hash) {
        oss << std::setw(2) << static_cast<int>(c);
    }
    return oss.str();
}

}  // namespace

std::string generate_hex_id(std::size_t length) {
    static const char* hex_chars = "0123456789abcdef";
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, 15);

    std::string result;
    result.reserve(length);
    for (std::size_t i = 0; i < length; ++i) {
        result += hex_chars[dis(gen)];
    }
    return result;
}

std::string hash_password(const std::string& password) {
    // 生成 16 字节随机盐（32 个 hex 字符）
    std::string salt = generate_hex_id(32);
    std::string hash = sha256_hex(salt + password);
    // 存储格式: sha256$<salt>$<hash>
    return "sha256$" + salt + "$" + hash;
}

bool verify_password(const std::string& password, const std::string& password_hash) {
    // 解析存储格式 "sha256$<salt>$<hash>"
    const std::string prefix = "sha256$";
    if (password_hash.rfind(prefix, 0) != 0) {
        return false;
    }

    std::size_t first_dollar = password_hash.find('$');
    std::size_t second_dollar = password_hash.find('$', first_dollar + 1);
    if (first_dollar == std::string::npos || second_dollar == std::string::npos) {
        return false;
    }

    std::string salt = password_hash.substr(first_dollar + 1,
                                            second_dollar - first_dollar - 1);
    std::string expected_hash = password_hash.substr(second_dollar + 1);
    std::string actual_hash = sha256_hex(salt + password);

    return actual_hash == expected_hash;
}

bool starts_with(const std::string& text, const std::string& prefix) {
    if (prefix.length() > text.length()) {
        return false;
    }
    return text.compare(0, prefix.length(), prefix) == 0;
}

bool ends_with(const std::string& text, const std::string& suffix) {
    if (suffix.length() > text.length()) {
        return false;
    }
    return text.compare(text.length() - suffix.length(), suffix.length(), suffix) == 0;
}

std::string trim(const std::string& text) {
    size_t start = text.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) {
        return "";
    }
    size_t end = text.find_last_not_of(" \t\n\r");
    return text.substr(start, end - start + 1);
}

std::string url_decode(const std::string& text) {
    std::string result;
    result.reserve(text.size());

    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '%') {
            // 处理 %XX 转义
            if (i + 2 < text.size() &&
                std::isxdigit(static_cast<unsigned char>(text[i + 1])) &&
                std::isxdigit(static_cast<unsigned char>(text[i + 2]))) {
                std::string hex = text.substr(i + 1, 2);
                result += static_cast<char>(std::stoi(hex, nullptr, 16));
                i += 2;
            } else {
                // 非法转义，保留原字符
                result += text[i];
            }
        } else if (text[i] == '+') {
            // '+' 解码为空格
            result += ' ';
        } else {
            result += text[i];
        }
    }
    return result;
}

std::string get_cookie(const std::string& request, const std::string& name) {
    // 解析原始 HTTP 请求中的 "Cookie:" 头
    std::istringstream stream(request);
    std::string line;
    std::string cookie_header;
    bool found = false;
    bool first_line = true;

    while (std::getline(stream, line)) {
        if (first_line) {
            // 跳过请求行（GET /path HTTP/1.1）
            first_line = false;
            continue;
        }
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) {
            break;  // 请求头结束
        }

        std::size_t colon_pos = line.find(':');
        if (colon_pos == std::string::npos) {
            continue;
        }
        std::string key = trim(line.substr(0, colon_pos));
        std::string key_lower;
        key_lower.reserve(key.size());
        for (char c : key) {
            key_lower += static_cast<char>(
                std::tolower(static_cast<unsigned char>(c)));
        }
        if (key_lower == "cookie") {
            cookie_header = line.substr(colon_pos + 1);
            found = true;
            break;
        }
    }

    if (!found) {
        return "";
    }

    // 解析 name=value 对（以 ';' 分隔）
    std::size_t pos = 0;
    while (pos <= cookie_header.size()) {
        std::size_t end = cookie_header.find(';', pos);
        if (end == std::string::npos) {
            end = cookie_header.size();
        }

        std::string pair = trim(cookie_header.substr(pos, end - pos));
        if (!pair.empty()) {
            std::size_t eq_pos = pair.find('=');
            if (eq_pos != std::string::npos) {
                std::string key = trim(pair.substr(0, eq_pos));
                if (key == name) {
                    return trim(pair.substr(eq_pos + 1));
                }
            }
        }

        if (end == cookie_header.size()) {
            break;
        }
        pos = end + 1;
    }
    return "";
}