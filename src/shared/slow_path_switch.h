#pragma once

#include <functional>
#include <mutex>
#include <vector>

namespace shared {

// ============================================================
// SlowPathSwitch - 读路径自适应慢路径开关（阶段5 增强）
//
// 背景：读路径全异步（阶段5）在纯 L1 命中稳态有约 -20% 开销（每请求投递/
// 线程切换），但缓存失效频繁 / L2、L3 慢 / 读放大场景下同步读会被 miss 的
// 阻塞 I/O 占住消费者线程（隔离收益 +148~211%）。
//
// 本组件让读路径「常态走同步快路径，检测到慢路径时自动切异步」：
//   - observe(latency_us)：每次读请求处理后上报耗时（同步分支由消费者线程
//     上报；异步分支由 worker 线程上报）。耗时 > slow_threshold_us 记为慢样本。
//   - 滑动窗口统计最近 window 个样本中的慢样本数 total_。
//   - total_ >= on_count  → 切异步（async()==true，读请求投 worker 隔离阻塞）
//   - total_ <= off_count → 切回同步（async()==false，恢复快路径）
//   - on_count > off_count：滞回（hysteresis），防频繁乒乓。
//
// 线程安全：observe/async 均加锁，可被消费者线程与 worker 线程并发调用。
// ============================================================
class SlowPathSwitch {
public:
    struct Config {
        int window = 100;                  // 滑动窗口样本数
        int on_count = 40;                 // 窗口内慢样本 >= on_count → 切异步
        int off_count = 10;                // 窗口内慢样本 <= off_count → 切回同步
        int slow_threshold_us = 5000;      // 单样本耗时 > 该值（微秒）记为慢
    };

    // 嵌套 Config 的成员默认初始化器不能作包围类默认参数（"default member
    // initializer required before the end of its enclosing class"），故用委托构造。
    SlowPathSwitch() : SlowPathSwitch(Config{}) {}
    explicit SlowPathSwitch(Config cfg)
        : cfg_(normalize(cfg)),
          counts_(static_cast<std::size_t>(cfg_.window), 0) {}

    // 上报一个读请求耗时（微秒）；返回上报后当前是否应走异步。
    bool observe(int latency_us) {
        std::lock_guard<std::mutex> lk(m_);
        const bool slow = latency_us > cfg_.slow_threshold_us;
        total_ = total_ - counts_[static_cast<std::size_t>(pos_)] + (slow ? 1 : 0);
        counts_[static_cast<std::size_t>(pos_)] = slow ? 1 : 0;
        pos_ = (pos_ + 1) % cfg_.window;
        if (!async_ && total_ >= cfg_.on_count) {
            async_ = true;
            if (on_change_) {
                on_change_(true);
            }
        } else if (async_ && total_ <= cfg_.off_count) {
            async_ = false;
            if (on_change_) {
                on_change_(false);
            }
        }
        return async_;
    }

    // 当前是否应走异步（慢路径）。
    bool async() const {
        std::lock_guard<std::mutex> lk(m_);
        return async_;
    }

    int total_slow() const {
        std::lock_guard<std::mutex> lk(m_);
        return total_;
    }

    // 状态切换回调（true = 切异步，false = 切回同步）。在内部锁内同步调用，
    // 只做轻量日志/计数，勿反向调用本对象。
    using StateChangeCallback = std::function<void(bool async_on)>;
    void set_on_state_change(StateChangeCallback cb) {
        std::lock_guard<std::mutex> lk(m_);
        on_change_ = std::move(cb);
    }

private:
    static Config normalize(Config c) {
        if (c.window < 1) c.window = 1;
        if (c.on_count < 0 || c.on_count > c.window) c.on_count = c.window;
        if (c.off_count < 0) c.off_count = 0;
        if (c.off_count > c.window) c.off_count = c.window;
        if (c.on_count < c.off_count) c.on_count = c.off_count;
        if (c.slow_threshold_us < 0) c.slow_threshold_us = 0;
        return c;
    }

    mutable std::mutex m_;
    Config cfg_;
    std::vector<int> counts_;  // 环形缓冲：每个槽 0=快 / 1=慢
    int pos_ = 0;              // 下一个待覆盖槽
    int total_ = 0;            // 窗口内慢样本总数
    bool async_ = false;       // 当前是否处于异步（慢路径）态
    StateChangeCallback on_change_;  // 状态切换回调（锁内调用，轻量）
};

}  // namespace shared
