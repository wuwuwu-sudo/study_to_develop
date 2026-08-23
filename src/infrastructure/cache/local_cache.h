#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

#include "shared/circuit_breaker.h"

namespace infrastructure::cache {

// ============================================================
// 第一级：本地内存缓存
// - 线程安全（内部互斥锁）
// - 支持 TTL（惰性过期）
// - 容量上限：满时先清除已过期条目，仍满则淘汰过期时间最早的条目
// - 支持按前缀批量失效（clear_prefix），用于写操作后清理某商家的全部缓存
// ============================================================
class LocalCache {
public:
    LocalCache(size_t max_entries = 10000, int default_ttl_seconds = 60);

    std::optional<std::string> get(const std::string& key);
    void set(const std::string& key, const std::string& value, int ttl_seconds = -1);
    bool del(const std::string& key);
    void clear_prefix(const std::string& prefix);
    void clear();
    size_t size() const;

private:
    struct Entry {
        std::string value;
        // 过期时间点；max() 表示永不过期（本实现 ttl<=0 时用默认 TTL，恒为有限值）
        std::chrono::steady_clock::time_point expires_at;
    };

    std::chrono::steady_clock::time_point expiry_for(int ttl_seconds) const;
    void purge_expired_locked(std::chrono::steady_clock::time_point now);
    // 容量淘汰：先清除过期条目，若仍满则移除最早过期的条目（调用方须持有锁）
    void evict_one_locked();

    size_t max_entries_;
    int default_ttl_seconds_;
    // 单锁 + 单表：8 进程 × 单消费线程模型下，每进程仅消费线程访问本地缓存，
    // 16 片分片锁已无争用，简化为单一互斥锁。
    mutable std::mutex mutex_;
    std::unordered_map<std::string, Entry> entries_;
    // 条目总数（原子计数）：写路径均在 mutex_ 内维护，与 entries_.size() 一致
    std::atomic<std::size_t> count_{0};
    // 整缓存一个三态熔断器：操作连续失败达阈值即熔断（快速失败走上层降级），
    // 冷却后自动半开探测恢复。
    shared::CircuitBreaker breaker_{/*failure_threshold=*/5,
                                    /*cooldown=*/std::chrono::milliseconds(1000),
                                    /*half_open_max_probes=*/1};
};

}  // namespace infrastructure::cache
