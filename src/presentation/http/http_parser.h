#pragma once

#include <cstddef>
#include <string>
#include "presentation/http/http_request.h"

namespace presentation::http {

class HttpParser {
public:
    // 解析完整请求（要求 raw_request 恰好为单个请求）。
    bool parse(const std::string& raw_request, HttpRequest& request);

    // 从缓冲区解析"单个"完整请求（支持 keep-alive / 管道请求）。
    // 返回：
    //    1  = 成功解析一个请求，consumed 为该请求占用的字节数（缓冲中剩余字节属下一个请求）
    //    0  = 数据不完整（头部或 body 未收全），需等待更多数据
    //   -1  = 语法错误，应关闭连接
    int parse_one(const std::string& buffer, std::size_t& consumed, HttpRequest& request);
};

}  // namespace presentation::http
