#include "infrastructure/cache/multi_level_cache.h"

#include <utility>

#include "infrastructure/cache/local_cache.h"
#include "infrastructure/cache/redis_client.h"

namespace infrastructure::cache {

MultiLevelCache::MultiLevelCache(std::shared_ptr<LocalCache> local,
                                 std::shared_ptr<RedisClient> redis,
                                 RedisAsyncSubmit redis_async_submit)
    : local_(std::move(local))
    , redis_(std::move(redis))
    , redis_async_submit_(std::move(redis_async_submit)) {}

std::optional<std::string> MultiLevelCache::get(const std::string& key,
                                                const Loader& loader,
                                                int local_ttl_seconds,
                                                int redis_ttl_seconds) {
    // 保证 L1 的 TTL 不长于 L2，避免 L1 命中 Redis 已过期的数据
    if (redis_ttl_seconds > 0 &&
        (local_ttl_seconds <= 0 || local_ttl_seconds > redis_ttl_seconds)) {
        local_ttl_seconds = redis_ttl_seconds;
    }

    // ---- L1：本地内存 ----
    auto hit = local_->get(key);
    if (hit) {
        return hit;
    }

    // ---- L2：Redis ----
    if (redis_) {
        auto remote = redis_->get(key);
        if (remote) {
            local_->set(key, *remote, local_ttl_seconds);
            return remote;
        }
    }

    // ---- L3：数据源 ----
    if (!loader) {
        return std::nullopt;
    }
    auto data = loader();
    if (!data) {
        return std::nullopt;
    }
    set(key, *data, local_ttl_seconds, redis_ttl_seconds);
    return data;
}

void MultiLevelCache::set(const std::string& key, const std::string& value,
                          int local_ttl_seconds, int redis_ttl_seconds) {
    local_->set(key, value, local_ttl_seconds);
    if (!redis_) {
        return;
    }
    // 阶段4：配置了异步提交器时，L2 写（回填）投专用 Redis 线程，业务线程不阻塞
    if (redis_async_submit_) {
        auto r = redis_;
        redis_async_submit_([r, key, value, redis_ttl_seconds]() {
            r->set(key, value, redis_ttl_seconds);
        });
    } else {
        redis_->set(key, value, redis_ttl_seconds);
    }
}

bool MultiLevelCache::del(const std::string& key) {
    bool removed = local_->del(key);
    if (!redis_) {
        return removed;
    }
    if (redis_async_submit_) {
        auto r = redis_;
        redis_async_submit_([r, key]() { r->del(key); });
    } else {
        removed = redis_->del(key) || removed;
    }
    return removed;
}

int MultiLevelCache::clear_prefix(const std::string& prefix) {
    local_->clear_prefix(prefix);
    if (!redis_) {
        return 0;
    }
    if (redis_async_submit_) {
        auto r = redis_;
        redis_async_submit_([r, prefix]() { r->clear_prefix(prefix); });
        return 0;  // L2 已投专用线程异步清；调用方不依赖该返回值
    }
    return redis_->clear_prefix(prefix);
}

bool MultiLevelCache::redis_available() const {
    return redis_ != nullptr && redis_->available();
}

}  // namespace infrastructure::cache
