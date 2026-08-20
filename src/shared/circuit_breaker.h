#pragma once

// ============================================================
// shared/circuit_breaker.h
// 通用三态熔断器（Circuit Breaker）
//
// 状态机：CLOSED → OPEN → HALF_OPEN → CLOSED（或回 OPEN）
//   - CLOSED（关闭/正常）：请求放行；连续失败达到阈值 → 熔断转 OPEN
//   - OPEN（打开/熔断）：请求快速失败（调用方走降级路径）；冷却时间过后转 HALF_OPEN
//   - HALF_OPEN（半开/试探）：放行少量探测请求；成功 → 复位 CLOSED；失败 → 回 OPEN
//
// 目的：当某个被保护对象（如缓存分片）连续失败时，避免每次都把成本打到
// 失败路径（锁等待/异常/下游不可用），快速失败并给其冷却恢复机会。
//
// 线程安全：内部用互斥锁保护状态，允许多线程并发调用。
// 值语义：可默认构造/拷贝（拷贝取当前状态快照），可直接放入容器（如
// std::array<Shard, N> 的分片成员）。
// ============================================================

#include <chrono>
#include <cstddef>
#include <functional>
#include <mutex>

namespace shared {

enum class CircuitState {
    CLOSED,     // 关闭/正常：放行
    OPEN,       // 打开/熔断（全开）：快速失败
    HALF_OPEN   // 半开/试探：放行探测请求
};

// 状态名（日志用）："CLOSED" / "OPEN" / "HALF_OPEN"
inline const char* to_string(CircuitState state) {
    switch (state) {
        case CircuitState::CLOSED: return "CLOSED";
        case CircuitState::OPEN: return "OPEN";
        case CircuitState::HALF_OPEN: return "HALF_OPEN";
        default: return "UNKNOWN";
    }
}

class CircuitBreaker {
public:
    // 状态切换回调（from -> to）：熔断开启/半开/恢复等状态变化时触发（低频），
    // 供调用方输出维护日志；默认空（不输出）。回调在内部锁内同步调用，
    // 只应做日志等轻量、不反向调用本对象的工作。
    using StateChangeCallback = std::function<void(CircuitState from, CircuitState to)>;

    // failure_threshold: CLOSED 下连续失败多少次后熔断（>=1）
    // cooldown: OPEN 保持熔断的时长，过后自动转 HALF_OPEN
    // half_open_max_probes: HALF_OPEN 下最多放行多少个并发探测请求
    // on_state_change: 状态切换回调（可选）
    explicit CircuitBreaker(
        int failure_threshold = 5,
        std::chrono::milliseconds cooldown = std::chrono::milliseconds(1000),
        std::size_t half_open_max_probes = 1,
        StateChangeCallback on_state_change = nullptr)
        : failure_threshold_(failure_threshold > 0 ? failure_threshold : 1)
        , cooldown_(cooldown)
        , half_open_max_probes_(half_open_max_probes > 0 ? half_open_max_probes : 1)
        , on_state_change_(std::move(on_state_change)) {}

    // 运行时设置状态切换回调（线程安全；构造未传回调时可补充）
    void set_on_state_change(StateChangeCallback cb) {
        std::lock_guard<std::mutex> lock(mutex_);
        on_state_change_ = std::move(cb);
    }

    // 是否允许本次请求通过（不通过 = 熔断快速失败）
    bool allow() {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto now = Clock::now();
        switch (state_) {
            case CircuitState::CLOSED:
                return true;
            case CircuitState::OPEN:
                // 冷却时间已过 → 转 HALF_OPEN 放行一个探测
                if (now - opened_at_ >= cooldown_) {
                    transition(CircuitState::OPEN, CircuitState::HALF_OPEN);
                    half_open_probes_ = 1;  // 占住一个探测名额
                    return true;
                }
                return false;
            case CircuitState::HALF_OPEN:
                if (half_open_probes_ < half_open_max_probes_) {
                    ++half_open_probes_;
                    return true;  // 放行探测
                }
                return false;  // 探测名额已满
        }
        return false;
    }

    // 调用成功：CLOSED 复位失败计数；HALF_OPEN 探测成功 → 复位 CLOSED
    void record_success() {
        std::lock_guard<std::mutex> lock(mutex_);
        consecutive_failures_ = 0;
        if (state_ == CircuitState::HALF_OPEN) {
            transition(CircuitState::HALF_OPEN, CircuitState::CLOSED);
            half_open_probes_ = 0;
        }
    }

    // 调用失败：CLOSED 连续失败达阈值 → OPEN；HALF_OPEN 探测失败 → 回 OPEN
    void record_failure() {
        std::lock_guard<std::mutex> lock(mutex_);
        ++consecutive_failures_;
        switch (state_) {
            case CircuitState::CLOSED:
                if (consecutive_failures_ >= static_cast<std::size_t>(failure_threshold_)) {
                    transition(CircuitState::CLOSED, CircuitState::OPEN);
                    opened_at_ = Clock::now();
                }
                break;
            case CircuitState::HALF_OPEN:
                transition(CircuitState::HALF_OPEN, CircuitState::OPEN);
                opened_at_ = Clock::now();
                break;
            case CircuitState::OPEN:
                break;  // 已在熔断，无需变更
        }
    }

    CircuitState state() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return state_;
    }

    // 手动复位为 CLOSED（清空全部计数）
    void reset() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (state_ != CircuitState::CLOSED) {
            transition(state_, CircuitState::CLOSED);
        }
        consecutive_failures_ = 0;
        half_open_probes_ = 0;
    }

private:
    using Clock = std::chrono::steady_clock;

    // 状态切换：更新状态并在有回调时通知（必须在持锁时调用）
    void transition(CircuitState from, CircuitState to) {
        state_ = to;
        if (on_state_change_) {
            on_state_change_(from, to);
        }
    }

    int failure_threshold_;
    std::chrono::milliseconds cooldown_;
    std::size_t half_open_max_probes_;
    StateChangeCallback on_state_change_;

    mutable std::mutex mutex_;
    CircuitState state_ = CircuitState::CLOSED;
    std::size_t consecutive_failures_ = 0;  // CLOSED 下连续失败计数
    std::size_t half_open_probes_ = 0;      // HALF_OPEN 下已放行的探测数
    Clock::time_point opened_at_{};         // 进入 OPEN 的时间点
};

}  // namespace shared
