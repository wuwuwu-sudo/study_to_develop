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
// TaskQueue - 可配置消费者数任务队列
//
// 事件循环线程（生产者）把客户端处理任务入队，N 个消费线程
// 从共享队列按序拉取执行。
//
// 默认单消费者（consumer_count=1）：
//   - 8 进程 × 1 核部署，并行度来自进程数，FIFO 严格保序
// 可配 >1 消费者（如 2）：
//   - 缓解 head-of-line 阻塞：一个慢任务只阻塞一个消费者，
//     其他消费者仍可处理队列里的后续任务
//   - 注意 8 进程 × N 消费者会过订阅（如 8×2=16 线程抢 8 核），
//     需 A/B 实测权衡（见 docs 对比报告）
//
// 相比批量唤醒线程池（thread_pool，已移除）：无攒批/深度睡眠逻辑。
// ============================================================
class TaskQueue {
public:
    // consumer_count 为 0 时自动回退为 1
    explicit TaskQueue(std::size_t consumer_count = 1);
    ~TaskQueue();

    TaskQueue(const TaskQueue&) = delete;
    TaskQueue& operator=(const TaskQueue&) = delete;

    // 入队（已 shutdown 时返回 false）
    bool enqueue(std::function<void()> task);

    // 阻塞等待所有已入队任务执行完成（队列清空且无任务执行中）。
    void wait_all();

    // 停止接受新任务并回收所有消费线程（幂等；已入队任务排空后退出）。
    void shutdown();

    std::size_t consumer_count() const;
    bool is_running() const;

private:
    void consumer_loop();

    std::vector<std::thread> consumers_;            // 消费线程
    std::queue<std::function<void()>> tasks_;       // 共享任务队列
    std::mutex mutex_;                              // 保护任务队列
    std::condition_variable cv_;                    // 任务到达通知

    // 完成通知（wait_all 用；独立于任务队列锁）
    mutable std::mutex done_mutex_;
    std::condition_variable done_cv_;
    std::atomic<std::size_t> pending_{0};  // 队列中 + 执行中任务总数
    std::atomic<bool> running_{true};
};

}  // namespace shared
