# 阶段5（读路径异步化 / 全链路事件驱动）性能验证报告

- **日期**：2026-08-23
- **机器**：8 核 / 3.8Gi 内存（可用 ~1.2Gi，压测期间有负载波动），Linux
- **版本**：`make release`（阶段5：读路径异步化开关 `server.read_path_async`）
- **工具**：wrk 4.2.0 + redis-cli（`CLIENT PAUSE`）+ Python 编排（`tools/bench_stage5_read_async.py` + `tools/wrk_mixed_read.lua`）
- **测试数**：448 全绿

## 1. 目标与实现

阶段5 目标：把 **GET 读路径**也纳入阶段2 的「整链异步投递」机制，实现全链路事件驱动——读请求的完整处理链（中间件+路由，含 DB 读、L2/L3 读、缓存回填）投 DB 工作线程池执行，消费者线程不再被读 miss 的阻塞 I/O 占用。

**实现（改动收敛，handler/service 层零改动）**
- `HttpServer` 拦截条件由「非 GET 才异步」扩展为「非 GET 始终异步 **或** `server.read_path_async=1` 时 GET 也异步」；新增 `set_read_path_async(bool)`。
- `main.cpp` 读取 `server.read_path_async`（默认 0，`constants.h::DEFAULT_READ_PATH_ASYNC`），日志 `Read path async: 0/1`。
- 8 实例 + `config.json` 加 `server.read_path_async=0`（默认关：稳态 L1 命中 >99.99% 零阻塞，避免每请求投递/线程切换）。
- 冒烟：`Read path async: 1 (GET offloaded to DB worker pool, full event-driven)`；读/写/管道 GET（同连接 4 连）均 200，写路径不回归。

## 2. 实验 A：纯 L1 命中稳态（异步的开销）

单实例 c=128，热缓存（L1 命中）：

| 接口 | sync（read_async=0） | async（read_async=1） | 差异 |
|---|---|---|---|
| /api/shops | 85.1k | 67.8k | **-20%** |
| /api/dishes | ~78k | 61.0k / 47.7k | -22~39%（负载噪声大） |

**结论**：纯 L1 命中场景（本项目稳态，命中率 >99.99%），读异步每请求多一次任务投递 + 线程切换 + worker 排队，是**净开销（约 -20%）**。→ **默认关合理**；稳态不该付这个代价。

## 3. 实验 B：混合负载 + Redis 卡顿（异步的隔离收益）

**场景**：80% 热 key（L1 命中）+ 20% 冷 key（L1 miss → L2 读 → L3 DB 回填）；`CLIENT PAUSE 2000`（ALL，暂停读+写）每 5s 注入一次，模拟 Redis 变慢/卡顿。c=64。

| 组 | 模式 | Redis | QPS | p50 | p99 |
|---|---|---|---|---|---|
| 基线-sync | sync | 正常 | 25087 | 2.53ms | 5.00ms |
| 基线-async | async | 正常 | **62118** | **0.95ms** | **2.41ms** |
| pause-sync | sync | 卡顿 | 19158 | 2.64ms | 169.82ms |
| pause-async | async | 卡顿 | **49342 / 59642** | **1.12 / 0.99ms** | 172.79 / 174.59ms |

### 结论
- **有读 miss 的负载下，async 隔离收益巨大**：
  - 无 pause：async QPS +148%（62118 vs 25087）——即使 Redis 正常，冷 key 的 L2+DB 往返在 sync 下也阻塞消费者、拖累热 key；async 下 miss 投 worker，消费者专注热 key。
  - Redis 卡顿：async QPS **+158~211%**（49342-59642 vs 19158），p50 减半（~1.0 vs ~2.6ms）——冷 key 的 L2 读超时（300ms）从消费者线程剥离到 worker。
- **p99 两者相当（~170ms）**：冷 key 的 L2 读在 async 下仍由 worker 同步执行、等 300ms 读超时——尾部延迟受「冷 key 自身读超时」限制，异步不改变它（只改变它占用的是消费者还是 worker）。

## 4. 综合结论（全链路事件驱动的完整画像）

| 场景 | 读异步（read_path_async=1） |
|---|---|
| 纯 L1 命中稳态（>99.99%） | **-20% QPS**（投递/线程切换开销）→ 默认关 |
| 有读 miss（缓存冷/失效频繁） | **+148% QPS、p50 -58%**（阻塞从消费者剥离）→ 收益显著 |
| Redis/SQLite 慢或故障 | **+158~211% QPS**（隔离到 worker）→ 收益最大 |
| 冷 key 自身尾部延迟（p99） | 不变（worker 内等超时），但不再拖累其他连接 |

**建议**：稳态默认关（`read_path_async=0`，靠 L1 吸收）；缓存失效频繁、L2/L3 不稳、读放大场景置 1 开启全链路事件驱动。

## 5. 备注
- 读异步复用 DB 工作线程池（`db.worker_threads=2`）：miss 请求与写请求共享 worker；冷启动/大失效风暴下 worker 可能排队（消费者不阻塞，miss 自身延迟增加）。若需更强隔离可独立读池（`db.worker_threads` 上调）。
- 投递条件仍是「批首且 send_buf 为空」：管道内后续读请求等下次 EPOLLIN 再处理，与阶段2/3 语义一致。
- 448 测试全绿；读/写/管道冒烟通过；测试数据已清理。
