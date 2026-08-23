#pragma once

#include <exception>
#include <functional>
#include <future>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

#include "shared/task_queue.h"

namespace infrastructure::database {

// ============================================================
// DbWorkerPool - DB 工作线程池（阶段2 基础设施）
//
// 基于 shared::TaskQueue（N 个消费线程）的薄封装：
//   - submit(task, on_done)：在 worker 线程执行 task()（产出自包含的响应字节），
//     完成后在 worker 线程回调 on_done(result)
//   - submit_blocking(f)：返回 std::future，供测试/需要同步结果处使用
//
// 用途（阶段2 写请求异步化）：业务线程把写处理整条链提交到 worker 线程，
// 不阻塞自身；worker 完成后把结果投回 TaskQueue 续跑发送
// （见 http_server.cpp 阶段2 接线 + README 计划）。
//
// 线程安全：复用 shared::TaskQueue 的互斥/条件变量实现。
// ============================================================
class DbWorkerPool {
public:
    // worker_count 为 0 时自动回退为 1
    explicit DbWorkerPool(std::size_t worker_count = 2)
        : queue_(worker_count) {}

    // 提交任务：task() 返回响应字节；完成后在 worker 线程调用 on_done(result)。
    // 任务异常被吞掉（result 清空），不会使 worker 崩溃。
    void submit(std::function<std::string()> task,
                std::function<void(std::string)> on_done) {
        queue_.enqueue([task = std::move(task), on_done = std::move(on_done)]() mutable {
            std::string result;
            try {
                result = task();
            } catch (...) {
                result.clear();
            }
            on_done(std::move(result));
        });
    }

    // 阻塞式提交（返回 future），供测试/需要同步结果处使用。
    template <class F>
    auto submit_blocking(F&& f) -> std::future<typename std::invoke_result_t<F>> {
        using R = typename std::invoke_result_t<F>;
        auto p = std::make_shared<std::promise<R>>();
        std::future<R> fut = p->get_future();
        queue_.enqueue([f = std::forward<F>(f), p]() mutable {
            try {
                p->set_value(f());
            } catch (...) {
                try {
                    p->set_exception(std::current_exception());
                } catch (...) {
                }
            }
        });
        return fut;
    }

    // 停止接受新任务并回收 worker 线程（幂等；已入队任务排空后退出）。
    void shutdown() { queue_.shutdown(); }

    std::size_t worker_count() const { return queue_.consumer_count(); }
    bool is_running() const { return queue_.is_running(); }

private:
    shared::TaskQueue queue_;
};

}  // namespace infrastructure::database
