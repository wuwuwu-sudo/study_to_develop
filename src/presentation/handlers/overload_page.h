#pragma once

// ============================================================
// presentation/handlers/overload_page.h
// 过载 / 熔断 / 有界队列拒绝时的简单错误页
//
// 用户要求：熔断器开启或有界队列排队超时时，直接给客户端返回
// 简单错误页面——不构造复杂的 JSON 错误对象，不打印 ERROR 级别日志。
// 本函数输出固定静态 HTML（503），零动态构造、零日志，快速失败保护服务器。
// ============================================================

#include "presentation/http/http_response.h"

namespace presentation::handlers {

inline void write_overload_page(presentation::http::HttpResponse& response) {
    response.set_status(503);
    response.set_content_type("text/html; charset=utf-8");
    response.set_body(
        "<!DOCTYPE html>"
        "<html><head><meta charset='utf-8'>"
        "<title>503 服务繁忙</title></head>"
        "<body><h1>503 服务繁忙</h1>"
        "<p>系统繁忙，请稍后重试。</p>"
        "</body></html>");
}

}  // namespace presentation::handlers
