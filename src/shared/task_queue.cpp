#include "shared/task_queue.h"

#include <utility>

namespace shared {

TaskQueue::TaskQueue(std::size_t consumer_count) {
    if (consumer_count == 0) {
        consumer_count = 1;
    }
    consumers_.reserve(consumer_count);
    for (std::size_t i = 0; i < consumer_count; ++i) {
        consumers_.emplace_back([this] { consumer_loop(); });
    }
}

TaskQueue::~TaskQueue() {
    shutdown();
}

bool TaskQueue::enqueue(std::function<void()> task) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_.load(std::memory_order_relaxed)) {
            return false;
        }
        tasks_.push(std::move(task));
        pending_.fetch_add(1, std::memory_order_relaxed);
    }
    // 唤醒全部消费者：规模 1-3 下成本可忽略，保证任务被及时拉取
    cv_.notify_all();
    return true;
}

void TaskQueue::consumer_loop() {
    for (;;) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] { return !running_.load() || !tasks_.empty(); });
            if (!running_.load() && tasks_.empty()) {
                return;
            }
            task = std::move(tasks_.front());
            tasks_.pop();
        }

        try {
            task();
        } catch (...) {
            // 单个任务异常不应导致消费线程退出或进程崩溃
        }

        const std::size_t remain =
            pending_.fetch_sub(1, std::memory_order_relaxed) - 1;
        if (remain == 0) {
            std::lock_guard<std::mutex> lock(done_mutex_);
            done_cv_.notify_all();
        }
    }
}

void TaskQueue::wait_all() {
    std::unique_lock<std::mutex> lock(done_mutex_);
    done_cv_.wait(lock, [this] { return pending_.load() == 0; });
}

void TaskQueue::shutdown() {
    {
        std::lock_guard<std::mutex> lock(done_mutex_);
        if (!running_.exchange(false)) {
            return;  // 幂等
        }
    }
    // 唤醒所有消费线程（处理完剩余队列后退出）
    cv_.notify_all();
    for (auto& worker : consumers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    consumers_.clear();
}

std::size_t TaskQueue::consumer_count() const {
    return consumers_.size();
}

bool TaskQueue::is_running() const {
    return running_.load();
}

}  // namespace shared
