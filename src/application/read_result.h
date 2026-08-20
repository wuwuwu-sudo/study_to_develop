#pragma once

// ============================================================
// application/read_result.h
// 受保护读路径的返回结构
//
// 业务读路径（/api/dishes、/api/shops）通过 shared::RequestGuard
// （有界队列 + 熔断器 + 高水位降级）保护后返回本结构：
//   - body:   响应体（可能为 nullopt：内部失败或降级未命中）
//   - status: 保护器裁决结果
//       kOk       → 正常路径，body 为完整业务响应
//       kShed     → 高水位快速降级，body 为本地缓存（L1）数据或
//                   最小空成功响应（未做复杂业务处理）
//       kRejected → 熔断/队列超时，调用方直接回简单错误页
//                   （勿构造复杂 JSON、勿打印 ERROR 级别日志）
// ============================================================

#include <optional>
#include <string>

#include "shared/request_guard.h"

namespace application {

struct SerializedReadResult {
    std::optional<std::string> body;
    shared::RequestGuard::Result status = shared::RequestGuard::Result::kOk;
};

}  // namespace application
