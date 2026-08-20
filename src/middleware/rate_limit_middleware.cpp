#include "middleware/rate_limit_middleware.h"

#include <array>
#include <chrono>
#include <cctype>
#include <cstddef>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>

#include "infrastructure/common/logger.h"

namespace presentation::middleware {

using infrastructure::common::Logger;

namespace {

// 默认配置（用于非法配置回退，防御性兜底）
constexpr int kDefaultMaxRequests = 100;
constexpr int kDefaultWindowSeconds = 60;
constexpr int kDefaultBlockSeconds = 300;

// 取逗号分隔的首个 token 并去除首尾空白，防止 X-Forwarded-For 被伪造堆叠
std::string first_token(const std::string& value) {
    std::size_t end = value.find(',');
    if (end == std::string::npos) {
        end = value.size();
    }
    std::size_t begin = 0;
    while (begin < end && std::isspace(static_cast<unsigned char>(value[begin]))) {
        ++begin;
    }
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }
    return value.substr(begin, end - begin);
}

}  // namespace

class RateLimitMiddleware::Impl {
public:
    struct Entry {
        std::size_t count = 0;
        std::chrono::steady_clock::time_point window_start =
            std::chrono::steady_clock::now();
        std::chrono::steady_clock::time_point blocked_until{};
    };

    // 按客户端 key 哈希分片：每片独立锁 + 独立 entries，避免单一全局锁把
    // 不同客户端的限流判定串行化；同 key（同客户端）恒进同片，限流语义不变。
    static constexpr std::size_t kShards = 16;
    struct Shard {
        std::mutex mutex;
        std::unordered_map<std::string, Entry> entries;
    };
    std::array<Shard, kShards> shards;

    int max_requests = kDefaultMaxRequests;
    int window_seconds = kDefaultWindowSeconds;
    int block_seconds = kDefaultBlockSeconds;

    Shard& shard_for(const std::string& key) {
        return shards[std::hash<std::string>{}(key) % kShards];
    }

    // 多级回退提取客户端标识：优先真实 IP，其次转发链，最后兜底 unknown
    // 防御：避免缺失 header 时所有客户端共享同一 key 导致全局限流
    std::string client_key(const presentation::http::HttpRequest& request) const {
        static const char* candidates[] = {"X-Real-IP", "X-Forwarded-For", "X-Client-IP"};
        for (const char* name : candidates) {
            const std::string value = first_token(request.header(name));
            if (!value.empty()) {
                return value;
            }
        }
        return "unknown";
    }
};

RateLimitMiddleware::RateLimitMiddleware()
    : impl_(std::make_unique<Impl>()) {}

RateLimitMiddleware::RateLimitMiddleware(const Config& config)
    : impl_(std::make_unique<Impl>()) {
    // 防御性编程：非法（<=0）配置回退到默认值，避免限流形同虚设或误拒所有请求
    impl_->max_requests =
        config.max_requests > 0 ? config.max_requests : kDefaultMaxRequests;
    impl_->window_seconds =
        config.window_seconds > 0 ? config.window_seconds : kDefaultWindowSeconds;
    impl_->block_seconds =
        config.block_seconds > 0 ? config.block_seconds : kDefaultBlockSeconds;
}

RateLimitMiddleware::~RateLimitMiddleware() = default;

void RateLimitMiddleware::handle(const presentation::http::HttpRequest& request,
                                 presentation::http::HttpResponse& response,
                                 Next next) {
    // 1. 锁外计算客户端标识（只读 request，无共享状态）
    const std::string key = impl_->client_key(request);
    const auto now = std::chrono::steady_clock::now();

    // 2. 锁内仅做限流状态判定与更新（微秒级窗口），
    //    绝不持有锁调用 next() —— 避免把完整请求处理串行化
    enum class Outcome { kAllowed, kBlocked, kExceeded };
    Outcome outcome = Outcome::kAllowed;
    {
        auto& shard = impl_->shard_for(key);
        std::lock_guard<std::mutex> lock(shard.mutex);
        auto& entry = shard.entries[key];

        // 封禁期：直接拒绝
        if (entry.blocked_until > now) {
            outcome = Outcome::kBlocked;
        } else {
            // 时间窗口过期：重置计数（保证限流随时间自动恢复）
            if (now - entry.window_start >= std::chrono::seconds(impl_->window_seconds)) {
                entry.count = 0;
                entry.window_start = now;
            }
            // 超限：进入封禁期
            if (entry.count >= static_cast<std::size_t>(impl_->max_requests)) {
                entry.blocked_until = now + std::chrono::seconds(impl_->block_seconds);
                outcome = Outcome::kExceeded;
            } else {
                ++entry.count;
                outcome = Outcome::kAllowed;
            }
        }
    }  // ← 锁在此释放

    // 3. 锁外处理结果：日志与 429 不再占用全局锁
    if (outcome != Outcome::kAllowed) {
        if (outcome == Outcome::kBlocked) {
            Logger::instance().warn("Rate limit: client blocked, key=" + key);
        } else {
            Logger::instance().warn("Rate limit: exceeded, key=" + key +
                                    ", block " + std::to_string(impl_->block_seconds) + "s");
        }
        response.set_status(429);
        response.set_text("Too Many Requests");
        return;
    }

    // 4. 放行：完整请求处理（后续中间件 + 路由 + DB/缓存）在锁外执行
    next();
}

}  // namespace presentation::middleware
