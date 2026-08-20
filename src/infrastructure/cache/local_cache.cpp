#include "infrastructure/cache/local_cache.h"

#include <algorithm>
#include <functional>
#include <utility>

#include "infrastructure/common/logger.h"

namespace infrastructure::cache {

namespace {
using Clock = std::chrono::steady_clock;

// 分片熔断状态切换日志：低频（仅状态变化时），WARN 级便于运维观察
//（分片连续失败熔断 / 冷却后半开 / 恢复等）。
const char* shard_state_log(shared::CircuitState s) { return shared::to_string(s); }
}  // namespace

LocalCache::LocalCache(size_t max_entries, int default_ttl_seconds)
    : max_entries_(max_entries > 0 ? max_entries : 1)
    , default_ttl_seconds_(default_ttl_seconds > 0 ? default_ttl_seconds : 60) {
    // 为每个分片熔断器注册状态切换日志回调（构造时一次性设置）
    for (std::size_t i = 0; i < shards_.size(); ++i) {
        shards_[i].breaker.set_on_state_change(
            [i](shared::CircuitState from, shared::CircuitState to) {
                infrastructure::common::Logger::instance().warn(
                    "local cache shard " + std::to_string(i) + " breaker: " +
                    shard_state_log(from) + " -> " + shard_state_log(to));
            });
    }
}

// 分片索引：按 key 哈希取模，同一 key 恒定落在同一分片（保证一致性）
LocalCache::Shard& LocalCache::shard_for(const std::string& key) {
    return shards_[std::hash<std::string>{}(key) % kShards];
}

const LocalCache::Shard& LocalCache::shard_for(const std::string& key) const {
    return shards_[std::hash<std::string>{}(key) % kShards];
}

Clock::time_point LocalCache::expiry_for(int ttl_seconds) const {
    if (ttl_seconds <= 0) {
        ttl_seconds = default_ttl_seconds_;
    }
    return Clock::now() + std::chrono::seconds(ttl_seconds);
}

std::optional<std::string> LocalCache::get(const std::string& key) {
    auto& shard = shard_for(key);
    // 熔断快速失败：OPEN 直接返回 miss，上层走 L2/L3 降级（不反复尝试失败路径）
    if (!shard.breaker.allow()) {
        return std::nullopt;
    }
    try {
        std::optional<std::string> result;
        {
            std::lock_guard<std::mutex> lock(shard.mutex);
            auto now = Clock::now();
            auto it = shard.entries.find(key);
            if (it == shard.entries.end()) {
                result = std::nullopt;
            } else if (it->second.expires_at != Clock::time_point::max() &&
                       now >= it->second.expires_at) {
                shard.entries.erase(it);
                count_.fetch_sub(1, std::memory_order_relaxed);
                result = std::nullopt;
            } else {
                result = it->second.value;
            }
        }
        shard.breaker.record_success();  // 命中/未命中/过期均为正常路径
        return result;
    } catch (...) {
        shard.breaker.record_failure();
        return std::nullopt;
    }
}

void LocalCache::set(const std::string& key, const std::string& value, int ttl_seconds) {
    Entry entry;
    entry.value = value;
    entry.expires_at = expiry_for(ttl_seconds);

    auto& shard = shard_for(key);
    // 熔断：丢弃写入（L2/L3 仍有数据，缓存缺失为降级，数据一致性不受影响）
    if (!shard.breaker.allow()) {
        return;
    }
    try {
        // 快速路径：key 已存在 → 仅锁本分片更新
        {
            std::lock_guard<std::mutex> lock(shard.mutex);
            auto it = shard.entries.find(key);
            if (it != shard.entries.end()) {
                it->second = std::move(entry);
                shard.breaker.record_success();
                return;
            }
        }
        // 需要插入：若已达全局容量上限，先淘汰一个。
        // 注意：此处不持任何分片锁（evict_one 内部按固定顺序加锁，避免死锁）
        if (count_.load(std::memory_order_relaxed) >= max_entries_) {
            evict_one();
        }
        // 插入（重查，防止并发下同一 key 已被其他线程插入）
        {
            std::lock_guard<std::mutex> lock(shard.mutex);
            auto it = shard.entries.find(key);
            if (it != shard.entries.end()) {
                it->second = std::move(entry);
            } else {
                shard.entries.emplace(key, std::move(entry));
                count_.fetch_add(1, std::memory_order_relaxed);
            }
        }
        shard.breaker.record_success();
    } catch (...) {
        shard.breaker.record_failure();
    }
}

bool LocalCache::del(const std::string& key) {
    auto& shard = shard_for(key);
    if (!shard.breaker.allow()) {
        return false;
    }
    try {
        bool removed;
        {
            std::lock_guard<std::mutex> lock(shard.mutex);
            removed = shard.entries.erase(key) > 0;
            if (removed) {
                count_.fetch_sub(1, std::memory_order_relaxed);
            }
        }
        shard.breaker.record_success();
        return removed;
    } catch (...) {
        shard.breaker.record_failure();
        return false;
    }
}

void LocalCache::clear_prefix(const std::string& prefix) {
    // 前缀无法哈希到单一分片，需扫描全部分片
    for (auto& shard : shards_) {
        std::lock_guard<std::mutex> lock(shard.mutex);
        if (prefix.empty()) {
            count_.fetch_sub(shard.entries.size(), std::memory_order_relaxed);
            shard.entries.clear();
            continue;
        }
        for (auto it = shard.entries.begin(); it != shard.entries.end();) {
            if (it->first.compare(0, prefix.size(), prefix) == 0) {
                count_.fetch_sub(1, std::memory_order_relaxed);
                it = shard.entries.erase(it);
            } else {
                ++it;
            }
        }
    }
}

void LocalCache::clear() {
    for (auto& shard : shards_) {
        std::lock_guard<std::mutex> lock(shard.mutex);
        shard.entries.clear();
    }
    count_.store(0, std::memory_order_relaxed);
}

size_t LocalCache::size() const {
    return count_.load(std::memory_order_relaxed);
}

void LocalCache::purge_expired_locked(Clock::time_point now, Shard& shard) {
    for (auto it = shard.entries.begin(); it != shard.entries.end();) {
        if (it->second.expires_at != Clock::time_point::max() &&
            now >= it->second.expires_at) {
            count_.fetch_sub(1, std::memory_order_relaxed);
            it = shard.entries.erase(it);
        } else {
            ++it;
        }
    }
}

void LocalCache::evict_one() {
    auto now = Clock::now();
    int victim_shard = -1;
    std::string victim_key;
    auto victim_expiry = Clock::time_point::max();
    // 第一遍：按固定顺序加锁扫描全部分片，找全局最早过期条目
    //（锁逐个获取/释放，无嵌套锁，不会死锁）
    for (std::size_t i = 0; i < shards_.size(); ++i) {
        auto& shard = shards_[i];
        std::lock_guard<std::mutex> lock(shard.mutex);
        purge_expired_locked(now, shard);
        for (const auto& [key, e] : shard.entries) {
            if (e.expires_at < victim_expiry) {
                victim_expiry = e.expires_at;
                victim_key = key;
                victim_shard = static_cast<int>(i);
            }
        }
    }
    // 第二遍：重新锁住 victim 所在分片后按 key 删除（避免悬垂迭代器）
    if (victim_shard >= 0) {
        auto& shard = shards_[victim_shard];
        std::lock_guard<std::mutex> lock(shard.mutex);
        if (shard.entries.erase(victim_key) > 0) {
            count_.fetch_sub(1, std::memory_order_relaxed);
        }
    }
}

}  // namespace infrastructure::cache
