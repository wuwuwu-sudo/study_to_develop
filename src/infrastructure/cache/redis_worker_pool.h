#pragma once

#include <cstddef>
#include <functional>

#include "shared/task_queue.h"

namespace infrastructure::cache {

// ============================================================
// RedisWorkerPool - Redis 专用工作线程池（阶段4）
//
// 基于 shared::TaskQueue 的薄封装（fire-and-forget）：
//   - submit(fn)：把 Redis 操作（set/del/clear_prefix）投到专用线程执行，
//     业务线程不阻塞在 Redis socket 上（消除 Redis 阻塞占用消费者线程）。
//   - shutdown()：排空已入队任务后回收线程（幂等）。
//
// 线程安全：RedisClient 内部已用 mutex 串行所有命令，
//   worker 线程调用它安全（同一时刻仅一个 worker 执行 Redis 命令）。
// ============================================================
class RedisWorkerPool {
public:
    // worker_count 为 0 时自动回退为 1
    explicit RedisWorkerPool(std::size_t worker_count = 1)
        : queue_(worker_count) {}

    // 提交任务（fire-and-forget）：task 在 worker 线程执行；异常被吞（不使 worker 崩溃）。
    void submit(std::function<void()> task) {
        queue_.enqueue([task = std::move(task)]() mutable {
            try {
                task();
            } catch (...) {
                // Redis 操作异常被吞掉；RedisClient 自身已优雅降级（返回 nullopt/false）
            }
        });
    }

    void shutdown() { queue_.shutdown(); }

    std::size_t worker_count() const { return queue_.consumer_count(); }
    bool is_running() const { return queue_.is_running(); }

private:
    shared::TaskQueue queue_;
};

}  // namespace infrastructure::cache
