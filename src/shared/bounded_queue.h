#pragma once

// ============================================================
// shared/bounded_queue.h
// 有界并发队列
//
// 目的：限制进入受保护区域的并发任务数，防止流量涌入压垮下游
//（熔断器开启 / 下游故障时）。容量固定，满时：
//   - try_push：立即失败（快速拒绝）
//   - push_timeout：等待名额至多 timeout，超时失败（中断任务，调用方快速降级）
// 提供 usage_ratio() 查询使用率，用于"高水位（>80%）快速降级"策略。
//
// 线程安全：内部互斥锁 + 条件变量。
// ============================================================

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <optional>

namespace shared {

template <typename T>
class BoundedQueue {
public:
    explicit BoundedQueue(std::size_t capacity)
        : capacity_(capacity > 0 ? capacity : 1) {}

    // 非阻塞入队：满 → 返回 false（快速拒绝）
    bool try_push(T value) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.size() >= capacity_) {
            return false;
        }
        queue_.push_back(std::move(value));
        cv_.notify_one();
        return true;
    }

    // 入队并等待名额至多 timeout；超时 → 返回 false（调用方中断任务、快速降级）
    bool push_timeout(T value, std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (!cv_.wait_for(lock, timeout, [this] { return queue_.size() < capacity_; })) {
            return false;
        }
        queue_.push_back(std::move(value));
        cv_.notify_one();
        return true;
    }

    // 非阻塞出队：空 → nullopt
    std::optional<T> try_pop() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) {
            return std::nullopt;
        }
        T value = std::move(queue_.front());
        queue_.pop_front();
        cv_.notify_one();
        return value;
    }

    // 出队并等待至多 timeout；超时 → nullopt
    std::optional<T> pop_timeout(std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (!cv_.wait_for(lock, timeout, [this] { return !queue_.empty(); })) {
            return std::nullopt;
        }
        T value = std::move(queue_.front());
        queue_.pop_front();
        cv_.notify_one();
        return value;
    }

    std::size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

    std::size_t capacity() const { return capacity_; }

    // 使用率 0.0 ~ 1.0（当前大小 / 容量），用于高水位降级判断
    double usage_ratio() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return static_cast<double>(queue_.size()) / static_cast<double>(capacity_);
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.clear();
        cv_.notify_all();
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<T> queue_;
    std::size_t capacity_;
};

}  // namespace shared
