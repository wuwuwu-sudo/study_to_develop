#pragma once

namespace constants {

inline constexpr int DEFAULT_PORT = 8080;
inline constexpr int DEFAULT_DB_POOL_SIZE = 4;
inline constexpr int DEFAULT_SESSION_TTL_SECONDS = 3600;
inline constexpr int DEFAULT_SESSION_CLEANUP_INTERVAL_SECONDS = 300;
// epoll 事件循环线程池大小（0 = 不使用线程池）
inline constexpr int DEFAULT_THREAD_POOL_SIZE = 4;

// 多级缓存（L1 本地内存 / L2 Redis / L3 数据源）
inline constexpr int DEFAULT_CACHE_TTL_SECONDS = 60;             // L1 本地缓存 TTL
inline constexpr int DEFAULT_CACHE_REDIS_TTL_SECONDS = 300;      // L2 Redis 缓存 TTL
inline constexpr int DEFAULT_CACHE_LOCAL_MAX_ENTRIES = 10000;    // L1 本地缓存容量上限

}  // namespace constants
