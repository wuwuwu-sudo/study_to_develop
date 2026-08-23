#pragma once

namespace constants {

inline constexpr int DEFAULT_PORT = 8080;
inline constexpr int DEFAULT_DB_POOL_SIZE = 4;
// 阶段2：DB 工作线程池默认线程数（写请求异步化）
inline constexpr int DEFAULT_DB_WORKER_THREADS = 2;
// 阶段4：Redis 专用工作线程默认线程数（L2 写操作异步化）
inline constexpr int DEFAULT_REDIS_WORKER_THREADS = 1;
inline constexpr int DEFAULT_SESSION_TTL_SECONDS = 3600;
inline constexpr int DEFAULT_SESSION_CLEANUP_INTERVAL_SECONDS = 300;
// 任务队列开关（1 = 事件循环提交到任务队列；0 = 事件循环内同步处理）
inline constexpr int DEFAULT_TASK_QUEUE_ENABLED = 1;
// 任务队列消费线程数（默认 1；>1 缓解单任务阻塞，需权衡 8 进程过订阅）
inline constexpr int DEFAULT_TASK_QUEUE_CONSUMERS = 1;
// 阶段5：读路径异步化强制开关（1 = GET 读请求也投 DB 工作线程池整链执行，全链路事件驱动）
inline constexpr int DEFAULT_READ_PATH_ASYNC = 0;
// 阶段5 增强：读路径自适应开关（1 = 常态同步快路径，检测到慢路径自动切异步；默认）
inline constexpr int DEFAULT_READ_PATH_AUTO = 1;
// 自适应慢路径阈值（读请求处理耗时，毫秒；超过则计慢样本）
inline constexpr int DEFAULT_READ_PATH_SLOW_THRESHOLD_MS = 5;

// 多级缓存（L1 本地内存 / L2 Redis / L3 数据源）
inline constexpr int DEFAULT_CACHE_TTL_SECONDS = 60;             // L1 本地缓存 TTL
inline constexpr int DEFAULT_CACHE_REDIS_TTL_SECONDS = 300;      // L2 Redis 缓存 TTL
inline constexpr int DEFAULT_CACHE_LOCAL_MAX_ENTRIES = 10000;    // L1 本地缓存容量上限

}  // namespace constants
