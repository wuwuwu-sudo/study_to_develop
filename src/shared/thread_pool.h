#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace shared {

// ============================================================
// ThreadPool - 批量唤醒线程池（全局共享队列）
//
// 全局共享任务队列 + 单锁 + 单条件变量：
//   - 任务数达到 batch_wake_threshold_（=线程数）→ notify_all，全员并行开工
//   - 空队列首条任务 → notify_one（pending_wake_ 防重复唤醒）
//   - worker 处理完一批进入 0.5ms 攒批窗口，窗口过期无新任务 → 深度睡眠
//     （空闲零空转）
// ============================================================
class ThreadPool {
public:
    // thread_count 为 0 时自动回退为 1
    explicit ThreadPool(std::size_t thread_count);
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    // 提交任务（线程池已 shutdown 时返回 false）
    bool enqueue(std::function<void()> task);

    // 阻塞等待所有已提交任务执行完成（队列清空且无任务执行中）。
    void wait_all();

    // 停止接受新任务并回收所有工作线程（幂等；已入队任务排空后退出）。
    void shutdown();

    std::size_t thread_count() const;
    bool is_running() const;

private:
    void worker_loop();

    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;  // 全局共享任务队列
    std::mutex mutex_;                          // 保护任务队列
    std::condition_variable cv_;                // 任务到达通知
    std::size_t batch_wake_threshold_ = 1;      // 达到该任务数批量唤醒全员
    std::atomic<bool> pending_wake_{false};     // 防重复唤醒标志（少量任务场景）

    // 完成通知（wait_all 用；独立于任务队列锁）
    mutable std::mutex done_mutex_;
    std::condition_variable done_cv_;
    std::atomic<std::size_t> pending_tasks_{0};  // 队列中 + 执行中任务总数
    std::atomic<bool> running_{true};
};

}  // namespace shared
