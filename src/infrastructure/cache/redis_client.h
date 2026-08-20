#pragma once

#include <chrono>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace infrastructure::cache {

// ============================================================
// 第二级：Redis 缓存客户端（RESP 协议，自实现，无第三方依赖）
// - 线程安全：所有命令通过内部互斥锁串行执行
// - 优雅降级：Redis 不可用时返回 nullopt/false，绝不抛出异常，
//   由调用方（MultiLevelCache）自动落到第三级（数据源）
// - 故障退避：连接失败后进入退避期，避免每个请求都尝试重连
// - 方法均为 virtual，便于测试使用内存版 Mock 替换
// ============================================================
class RedisClient {
public:
    RedisClient(std::string host, int port,
                int connect_timeout_ms = 500, int read_timeout_ms = 500);
    virtual ~RedisClient();

    RedisClient(const RedisClient&) = delete;
    RedisClient& operator=(const RedisClient&) = delete;

    virtual std::optional<std::string> get(const std::string& key);
    virtual bool set(const std::string& key, const std::string& value, int ttl_seconds);
    virtual bool del(const std::string& key);
    // 用 SCAN + DEL 删除匹配前缀的所有 key；返回删除数量，失败返回 -1
    virtual int clear_prefix(const std::string& prefix);
    virtual bool ping();
    virtual bool available() const;

private:
    // RESP 回复的轻量结构
    struct Reply {
        enum class Type { Nil, Status, Integer, Bulk, Array, Error };
        Type type = Type::Nil;
        std::string str;  // Status / Bulk / Error 文本
        long long integer = 0;
        std::vector<Reply> elements;
    };

    bool ensure_connected_locked();
    void fail_locked();
    bool send_command_locked(const std::vector<std::string>& argv);
    bool read_reply_locked(Reply& reply, int depth = 0);
    bool read_line_locked(std::string& line);
    bool read_n_locked(std::string& out, size_t n);

    std::string host_;
    int port_;
    int connect_timeout_ms_;
    int read_timeout_ms_;

    int fd_ = -1;
    std::string inbuf_;  // socket 上未消费的字节
    mutable std::mutex mutex_;
    std::chrono::steady_clock::time_point retry_after_{};
    std::chrono::seconds retry_backoff_{30};
};

}  // namespace infrastructure::cache
