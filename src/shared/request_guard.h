#pragma once

// ============================================================
// shared/request_guard.h
// 请求保护器：有界队列 + 三态熔断器 + 高水位快速降级
//
// 目的：在熔断器上游再加一道有界队列闸门，防止熔断器开启 / 下游故障时
// 大量流量涌入导致服务器过载崩溃。执行受保护任务时的流程：
//
//   1. 高水位（队列使用率 > high_watermark，默认 80%）→ 快速降级：
//        调用方走"本地缓存快路径"返回，不做复杂业务处理（不排队、不查下游）。
//   2. 熔断器 OPEN → 拒绝：直接返回，调用方回简单错误（勿构造复杂 JSON、
//        勿打 ERROR 日志）。
//   3. 有界入队（等待名额至多 queue_wait_timeout）：超时 → 中断任务并拒绝
//        （调用方回简单错误页）。
//   4. 执行任务；成功/失败记录到熔断器（连续失败超阈值 → 熔断）。
//
// 线程安全：BoundedQueue 与 CircuitBreaker 各自内部加锁。
// 开销：正常低负载下每次执行仅一次使用率判断 + 一次入队/出队（互斥计数），
//       对热路径影响可忽略。
// ============================================================

#include <atomic>
#include <chrono>
#include <cstddef>
#include <functional>
#include <string>
#include <utility>

#include "shared/bounded_queue.h"
#include "shared/circuit_breaker.h"

namespace shared {

class RequestGuard {
public:
    struct Config {
        std::size_t max_queue = 256;                          // 有界队列容量
        double high_watermark = 0.8;                          // 使用率阈值（> 触发降级）
        std::chrono::milliseconds queue_wait_timeout{50};     // 排队等待名额超时（ms）
        int failure_threshold = 5;                            // 熔断连续失败阈值
        std::chrono::milliseconds cooldown{1000};             // 熔断冷却时间
        std::size_t half_open_max_probes = 1;                 // 半开探测名额

        // 降级执行日志抽样分母（0=关闭；1=全量；N=每 N 条降级记录 1 条）。
        // 降级（高水位/熔断/队列超时）可能在高负载下高频发生，逐条记录会刷屏
        // 并放大 I/O，故按比例采样，仅保留维护所需的低频样本。
        std::size_t shed_log_sample = 100;
        // 降级执行日志回调（可选）：命中采样时回调，参数为降级原因描述。
        std::function<void(const std::string&)> on_shed_log;
        // 熔断状态切换日志回调（可选）：CLOSED<->OPEN<->HALF_OPEN 状态变化时触发。
        std::function<void(CircuitState from, CircuitState to)> on_breaker_state;
    };

    enum class Result {
        kOk,        // 任务已正常执行
        kShed,      // 高水位快速降级（调用方用本地缓存返回，未执行复杂任务）
        kRejected   // 熔断 / 队列满超时（调用方回简单错误页）
    };

    explicit RequestGuard(const Config& cfg)
        : cfg_(cfg)
        , queue_(cfg.max_queue > 0 ? cfg.max_queue : 1)
        , breaker_(cfg.failure_threshold, cfg.cooldown, cfg.half_open_max_probes,
                   cfg.on_breaker_state) {}

    // 默认构造（使用 Config 默认值）；委托构造避免嵌套类型作默认参数的限制
    RequestGuard() : RequestGuard(Config{}) {}

    // 执行受保护任务。
    //   task:     复杂业务（返回 bool 表示成功），仅在放行且未降级时执行
    //   degraded: 降级回调（使用本地缓存快路径，不做复杂业务处理）
    template <typename Fn, typename DegradedFn>
    Result execute(Fn&& task, DegradedFn&& degraded) {
        // 1. 高水位 → 快速降级：用本地缓存，不排队、不执行复杂业务
        if (queue_.usage_ratio() > cfg_.high_watermark) {
            degraded();
            maybe_log_shed("high_watermark: shed to local cache");
            return Result::kShed;
        }
        // 2. 熔断 OPEN → 快速拒绝（调用方回简单错误页）
        if (!breaker_.allow()) {
            maybe_log_shed("breaker open: rejected");
            return Result::kRejected;
        }
        // 3. 有界入队：等待名额至多 queue_wait_timeout；超时 → 中断并拒绝
        if (!queue_.push_timeout(Token{}, cfg_.queue_wait_timeout)) {
            maybe_log_shed("queue timeout: rejected");
            return Result::kRejected;
        }
        // 4. 执行复杂任务（异常按失败处理，避免异常向上传播击穿服务器）
        bool ok = false;
        try {
            ok = task();
        } catch (...) {
            ok = false;
        }
        queue_.try_pop();  // 释放名额（同步执行，队列中停留仅一瞬间）
        if (ok) {
            breaker_.record_success();
            return Result::kOk;
        }
        breaker_.record_failure();
        return Result::kRejected;
    }

    double usage_ratio() const { return queue_.usage_ratio(); }
    CircuitState breaker_state() const { return breaker_.state(); }
    std::size_t queue_size() const { return queue_.size(); }
    std::size_t max_queue() const { return cfg_.max_queue; }
    double high_watermark() const { return cfg_.high_watermark; }

private:
    struct Token {};  // 队列占位（仅作并发名额计数）

    // 降级执行日志（按 cfg_.shed_log_sample 比例采样）：
    //   0 = 关闭；1 = 全量；N = 每 N 条记 1 条（第 1、1+N、1+2N... 条）。
    void maybe_log_shed(const char* reason) {
        if (cfg_.shed_log_sample == 0 || !cfg_.on_shed_log) {
            return;
        }
        const std::size_t n =
            shed_log_counter_.fetch_add(1, std::memory_order_relaxed) + 1;
        if ((n - 1) % cfg_.shed_log_sample == 0) {
            cfg_.on_shed_log(std::string(reason));
        }
    }

    Config cfg_;
    BoundedQueue<Token> queue_;
    CircuitBreaker breaker_;
    std::atomic<std::size_t> shed_log_counter_{0};
};

}  // namespace shared
