#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace infrastructure::cache {

class LocalCache;
class RedisClient;

// ============================================================
// 三级缓存门面：L1 本地内存 → L2 Redis → L3 数据源(loader)
//
// get(key, loader):
//   1) 查 L1（本地内存）
//   2) L1 未命中则查 L2（Redis）
//   3) L2 也未命中（或 Redis 不可用）则调用 loader 从数据源加载，
//      并把结果回填到 L2、L1（写回缓存，缓存旁路 Cache-Aside）
//
// - 任一缓存层故障自动降级，不影响数据正确性
// - 失效：del 删除单键 / clear_prefix 按前缀批量失效（写操作后调用）
// - 约束：L1 的 TTL 会被钳制为不超过 L2 的 TTL，避免 L1 命中已过期数据
// ============================================================
class MultiLevelCache {
public:
    using Loader = std::function<std::optional<std::string>()>;
    // 阶段4：Redis 异步提交器（把 Redis 写操作投到专用线程，业务线程不阻塞）。
    // nullptr = 同步执行（向后兼容）；非空时 set/del/clear_prefix 的 L2 部分走异步。
    using RedisAsyncSubmit = std::function<void(std::function<void()>)>;

    MultiLevelCache(std::shared_ptr<LocalCache> local,
                    std::shared_ptr<RedisClient> redis,
                    RedisAsyncSubmit redis_async_submit = nullptr);

    std::optional<std::string> get(const std::string& key, const Loader& loader,
                                   int local_ttl_seconds = -1,
                                   int redis_ttl_seconds = -1);
    void set(const std::string& key, const std::string& value,
             int local_ttl_seconds = -1, int redis_ttl_seconds = -1);
    bool del(const std::string& key);
    // 按前缀清理 L1 + L2。同步模式返回 Redis 删除数；异步模式返回 0（L2 已投专用线程异步清）。
    int clear_prefix(const std::string& prefix);

    bool redis_available() const;
    std::shared_ptr<LocalCache> local() const { return local_; }

private:
    std::shared_ptr<LocalCache> local_;
    std::shared_ptr<RedisClient> redis_;
    RedisAsyncSubmit redis_async_submit_;
};

}  // namespace infrastructure::cache
