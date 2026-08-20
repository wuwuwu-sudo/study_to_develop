#include "shared/thread_pool.h"

#include <chrono>
#include <utility>

namespace shared {

namespace {
// 攒批窗口：worker 处理完一批后，最多再等 500us 收集少量新任务一起处理
// （减少每任务一次信号/上下文切换）；窗口过期无新任务 → 深度睡眠。
constexpr auto kBatchWakeInterval = std::chrono::microseconds(500);
}  // namespace

ThreadPool::ThreadPool(std::size_t thread_count) {
    if (thread_count == 0) {
        thread_count = 1;
    }
    batch_wake_threshold_ = thread_count;
    workers_.reserve(thread_count);
    for (std::size_t i = 0; i < thread_count; ++i) {
        workers_.emplace_back([this] { worker_loop(); });
    }
}

ThreadPool::~ThreadPool() {
    shutdown();
}

bool ThreadPool::enqueue(std::function<void()> task) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_.load(std::memory_order_relaxed)) {
            return false;
        }
        const bool was_empty = tasks_.empty();
        tasks_.push(std::move(task));
        pending_tasks_.fetch_add(1, std::memory_order_relaxed);
        const std::size_t n = tasks_.size();
        if (n >= batch_wake_threshold_) {
            // 达到阈值：批量唤醒所有 worker 并行开工
            cv_.notify_all();
        } else if (was_empty && !pending_wake_.exchange(true)) {
            // 空队列首条任务：唤醒一个 worker（pending_wake_ 防重复唤醒）
            cv_.notify_one();
        }
        // 队列非空且未达阈值：已有 worker 在攒批窗口消费，无需再唤醒
    }
    return true;
}

void ThreadPool::worker_loop() {
    // 攒批窗口：处理完一批后进入 0.5ms 窗口，期间到达的少量任务由本 worker
    // 连续消费（无需逐条唤醒）；窗口过期且无新任务 → 转深度睡眠（空闲零空转）。
    bool drain_window = false;
    for (;;) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            const auto pred = [&] { return !running_.load() || !tasks_.empty(); };
            if (drain_window) {
                // 攒批窗口：最多等 0.5ms 收集新任务；超时无任务 → 转深度睡眠
                if (!cv_.wait_for(lock, kBatchWakeInterval, pred)) {
                    drain_window = false;
                    // 转入深度睡眠：重置唤醒标志，下次空队列来任务需重新唤醒
                    pending_wake_.store(false, std::memory_order_relaxed);
                    cv_.wait(lock, pred);
                }
            } else {
                cv_.wait(lock, pred);
            }
            if (!running_.load() && tasks_.empty()) {
                return;
            }
            task = std::move(tasks_.front());
            tasks_.pop();
        }

        try {
            task();
        } catch (...) {
            // 单个任务异常不应导致工作线程退出或进程崩溃
        }

        const std::size_t remain =
            pending_tasks_.fetch_sub(1, std::memory_order_relaxed) - 1;
        if (remain == 0) {
            std::lock_guard<std::mutex> lock(done_mutex_);
            done_cv_.notify_all();
        }
        drain_window = true;  // 处理完一个任务：进入攒批窗口
    }
}

void ThreadPool::wait_all() {
    std::unique_lock<std::mutex> lock(done_mutex_);
    done_cv_.wait(lock, [this] { return pending_tasks_.load() == 0; });
}

void ThreadPool::shutdown() {
    {
        std::lock_guard<std::mutex> lock(done_mutex_);
        if (!running_.exchange(false)) {
            return;  // 幂等
        }
    }
    // 唤醒所有 worker（处理完剩余队列后退出）
    cv_.notify_all();
    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    workers_.clear();
}

std::size_t ThreadPool::thread_count() const {
    return workers_.size();
}

bool ThreadPool::is_running() const {
    return running_.load();
}

}  // namespace shared
