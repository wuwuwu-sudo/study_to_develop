#pragma once

#include <array>
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

    // 分片：每片独立互斥锁 + 子表，多线程访问不同 key 落在不同分片，
    // 将原来单个全局锁的争用降至 1/kShards（高并发读路径关键优化）。
    // 每片挂一个三态熔断器：某分片操作连续失败达阈值即熔断（快速失败走
    // 上层降级），冷却后自动半开探测恢复——分片间互相隔离，一片熔断不影响其他片。
    struct Shard {
        mutable std::mutex mutex;
        std::unordered_map<std::string, Entry> entries;
        shared::CircuitBreaker breaker{/*failure_threshold=*/5,
                                       /*cooldown=*/std::chrono::milliseconds(1000),
                                       /*half_open_max_probes=*/1};
    };

    std::chrono::steady_clock::time_point expiry_for(int ttl_seconds) const;
    Shard& shard_for(const std::string& key);
    const Shard& shard_for(const std::string& key) const;
    void purge_expired_locked(std::chrono::steady_clock::time_point now, Shard& shard);
    // 全局容量淘汰：扫描全部分片（按固定顺序加锁），移除全局最早过期条目
    void evict_one();

    // 分片数：每片独立锁，get/set/del 仅锁单分片，争用降至 1/kShards
    static constexpr std::size_t kShards = 16;
    size_t max_entries_;
    int default_ttl_seconds_;
    // 全局条目总数（原子计数）：保持“总容量 = max_entries_”的精确语义，
    // 不受分片影响（小容量缓存行为与改造前一致）
    std::atomic<std::size_t> count_{0};
    std::array<Shard, kShards> shards_;
};

}  // namespace infrastructure::cache
