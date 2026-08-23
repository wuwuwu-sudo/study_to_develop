#include "infrastructure/cache/local_cache.h"

#include <algorithm>
#include <functional>
#include <utility>

#include "infrastructure/common/logger.h"

namespace infrastructure::cache {

namespace {
using Clock = std::chrono::steady_clock;
}  // namespace

LocalCache::LocalCache(size_t max_entries, int default_ttl_seconds)
    : max_entries_(max_entries > 0 ? max_entries : 1)
    , default_ttl_seconds_(default_ttl_seconds > 0 ? default_ttl_seconds : 60) {
    // 为缓存熔断器注册状态切换日志回调（构造时一次性设置，WARN 级便于运维观察）
    breaker_.set_on_state_change([](shared::CircuitState from, shared::CircuitState to) {
        infrastructure::common::Logger::instance().warn(
            "local cache breaker: " + std::string(shared::to_string(from)) + " -> " +
            std::string(shared::to_string(to)));
    });
}

Clock::time_point LocalCache::expiry_for(int ttl_seconds) const {
    if (ttl_seconds <= 0) {
        ttl_seconds = default_ttl_seconds_;
    }
    return Clock::now() + std::chrono::seconds(ttl_seconds);
}

std::optional<std::string> LocalCache::get(const std::string& key) {
    // 熔断快速失败：OPEN 直接返回 miss，上层走 L2/L3 降级（不反复尝试失败路径）
    if (!breaker_.allow()) {
        return std::nullopt;
    }
    try {
        std::optional<std::string> result;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto now = Clock::now();
            auto it = entries_.find(key);
            if (it == entries_.end()) {
                result = std::nullopt;
            } else if (it->second.expires_at != Clock::time_point::max() &&
                       now >= it->second.expires_at) {
                entries_.erase(it);
                count_.fetch_sub(1, std::memory_order_relaxed);
                result = std::nullopt;
            } else {
                result = it->second.value;
            }
        }
        breaker_.record_success();  // 命中/未命中/过期均为正常路径
        return result;
    } catch (...) {
        breaker_.record_failure();
        return std::nullopt;
    }
}

void LocalCache::set(const std::string& key, const std::string& value, int ttl_seconds) {
    Entry entry;
    entry.value = value;
    entry.expires_at = expiry_for(ttl_seconds);

    // 熔断：丢弃写入（L2/L3 仍有数据，缓存缺失为降级，数据一致性不受影响）
    if (!breaker_.allow()) {
        return;
    }
    try {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = entries_.find(key);
        if (it != entries_.end()) {
            // 快速路径：key 已存在 → 直接更新
            it->second = std::move(entry);
        } else {
            // 需要插入：若已达容量上限，先淘汰一个（单锁下直接淘汰，无并发插入竞争）
            if (count_.load(std::memory_order_relaxed) >= max_entries_) {
                evict_one_locked();
            }
            entries_.emplace(key, std::move(entry));
            count_.fetch_add(1, std::memory_order_relaxed);
        }
        breaker_.record_success();
    } catch (...) {
        breaker_.record_failure();
    }
}

bool LocalCache::del(const std::string& key) {
    if (!breaker_.allow()) {
        return false;
    }
    try {
        bool removed;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            removed = entries_.erase(key) > 0;
            if (removed) {
                count_.fetch_sub(1, std::memory_order_relaxed);
            }
        }
        breaker_.record_success();
        return removed;
    } catch (...) {
        breaker_.record_failure();
        return false;
    }
}

void LocalCache::clear_prefix(const std::string& prefix) {
    // 前缀失效是数据一致性关键，不经过熔断（同原实现：clear 类操作不熔断）
    std::lock_guard<std::mutex> lock(mutex_);
    if (prefix.empty()) {
        count_.store(0, std::memory_order_relaxed);
        entries_.clear();
        return;
    }
    for (auto it = entries_.begin(); it != entries_.end();) {
        if (it->first.compare(0, prefix.size(), prefix) == 0) {
            count_.fetch_sub(1, std::memory_order_relaxed);
            it = entries_.erase(it);
        } else {
            ++it;
        }
    }
}

void LocalCache::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    entries_.clear();
    count_.store(0, std::memory_order_relaxed);
}

size_t LocalCache::size() const {
    return count_.load(std::memory_order_relaxed);
}

void LocalCache::purge_expired_locked(Clock::time_point now) {
    for (auto it = entries_.begin(); it != entries_.end();) {
        if (it->second.expires_at != Clock::time_point::max() &&
            now >= it->second.expires_at) {
            count_.fetch_sub(1, std::memory_order_relaxed);
            it = entries_.erase(it);
        } else {
            ++it;
        }
    }
}

void LocalCache::evict_one_locked() {
    auto now = Clock::now();
    purge_expired_locked(now);
    // 清除过期后仍满：移除最早过期的条目（单锁下一次性遍历，无悬垂迭代器问题）
    if (count_.load(std::memory_order_relaxed) < max_entries_) {
        return;
    }
    auto victim = entries_.begin();
    if (victim == entries_.end()) {
        return;
    }
    for (auto it = entries_.begin(); it != entries_.end(); ++it) {
        if (it->second.expires_at < victim->second.expires_at) {
            victim = it;
        }
    }
    entries_.erase(victim);
    count_.fetch_sub(1, std::memory_order_relaxed);
}

}  // namespace infrastructure::cache
