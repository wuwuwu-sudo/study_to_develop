# perf 性能剖析报告 v2（优化后）：webserver 单实例（经 Nginx 网关）

- **测试日期**：2026-08-15
- **架构**：wrk → **Nginx(:80)** → **webserver(8081 单实例)**
- **工具**：perf 7.0.12 + FlameGraph
- **被测**：release 构建（`-fno-omit-frame-pointer`，C++17, epoll + 8 线程池 + SQLite WAL）
- **本版已应用优化**：① 日志时间戳每秒缓存；② LocalCache 16 分片锁
- **采样**：`perf record -F 99 -g -e cpu-clock:u -p <PID> -- sleep 30`，**1913 样本**
- **负载**：wrk `-t4 -c64 -d40s http://127.0.0.1/api/shops`（经 Nginx），用户态 ~19.8s
- **产物**：`docs/perf_flamegraph_20260815_opt.svg`（优化后调用栈火焰图）

---

## 1. 优化效果总览（对比优化前）

| 指标 | 优化前 | **优化后** | 变化 |
|------|--------|-----------|------|
| `LoggingMiddleware::handle`（包含） | 20.32% | **2.72%** | **-87%** |
| `LoggerImpl::write`（包含） | 16.33% | **2.09%** | **-87%** |
| `time_put`/`strftime`（时间戳格式化） | ~7% | **0%** | 消除 |
| `__tz_convert`/`__tzset_parse_tz`/`parse_offset`/`sscanf`（时区解析） | **~5%** | **0%** | **彻底消除** |

> **核心结论**：日志路径从 ~20% 降到 ~2.7%——「日志时间戳每秒缓存」一次消除了 **tz 文件解析（~5%）+ 时间戳格式化（~7%）**，是全报告最大的单项优化。剩余日志成本来自异步写线程的批量落盘（`__ostream_insert`/`xsputn`），已移出请求线程热路径。

---

## 2. 优化后 Children / Self 权威表（perf report，调用链统计）

| Children(包含) | Self(自耗) | 函数 | 归属 |
|------:|------:|------|------|
| 8.42% | 0.84% | `HttpResponse::serialize` | 响应序列化 |
| 8.05% | **8.05%** | `__libc_recv` | 网络接收 |
| 7.27% | 1.10% | `operator new` | 内存分配 |
| 6.48% | 3.24% | `cfree` | 内存释放 |
| 6.33% | 4.44% | `malloc` | 内存分配 |
| 5.80% | 2.40% | `__ostream_insert` | 日志流（写线程） |
| 5.75% | **5.75%** | `__send` | 网络发送 |
| 4.08% | **4.08%** | `pthread_mutex_lock` | 锁 |
| 3.87% | 0.26% | `ThreadPool::enqueue` | 线程池入队 |
| 3.61% | 3.61% | `_int_free` | 内存释放 |
| 3.45% | 3.45% | `__memmove_evex_unaligned_erms` | 内存拷贝 |
| 3.45% | 0.42% | `steady_clock::now` | 时间读取 |
| 3.08% | 0.58% | `clock_gettime` | 时间读取（syscall） |
| 2.98% | 2.98% | `pthread_cond_signal` | 条件变量 |
| 2.77% | 0.58% | `ThreadPool::worker_loop` | 线程池工作线程 |
| 2.67% | 1.10% | `basic_streambuf::xsputn` | 日志流 |
| 2.40% | 2.40% | `pthread_mutex_unlock` | 锁 |
| 2.30% | 0.84% | `HttpServer::sweep_idle_connections` | 空闲连接清扫 |
| 1.88% | 1.52% | `_int_malloc` | 内存分配 |
| 1.67% | 1.25% | `HttpServer::get_conn` | 连接表查询 |

> 注：仍有 ~32% 样本落在 `[unknown]`（syscall 边界坏栈，火焰图已剔除）。

---

## 3. 分类聚合（优化后）

| 类别 | 包含成本 | 说明 |
|------|---------|------|
| **内存分配/释放** | **~25%** | `operator new` 7.3% + `malloc` 6.3% + `cfree` 6.5% + `_int_*` 5.5% —— **已升至首位** |
| **网络 I/O（libc 包装）** | **~14%** | `__libc_recv` 8.1% + `__send` 5.8% |
| **锁/条件变量** | **~9.5%** | `mutex_lock/unlock` 6.5% + `cond_signal` 3.0%（线程池/连接表） |
| 响应序列化 + 拷贝 | ~12% | `serialize` 8.4% + `memmove` 3.5% |
| 时间读取 | ~6.5% | `steady_clock::now` + `clock_gettime` |
| **日志 IO** | **~5.8%** | 已收敛到异步写线程 `__ostream_insert`/`xsputn`（不在请求热路径） |
| 框架调度 | ~10% | `ThreadPool::enqueue/worker_loop`、`event_loop`、`sweep_idle` |

---

## 4. 剩余瓶颈与下一步优化（按收益排序）

1. **内存分配/释放（~25%，现在最大头）**：每请求 `new`/`free`（响应串、缓冲区、DTO、函数对象）。
   - `operator new` 7.3% + `malloc`/`_int_malloc` 8.2% + `cfree`/`_int_free` 10% 合计约 25%。
   - 建议：① 连接/响应缓冲**池化复用**；② `std::string` 预分配（`reserve`）；③ 减少高频路径临时对象（lambda/`std::function` 已见 1%+）。

2. **锁/线程池（~9.5%）**：LocalCache 分片锁已消除缓存锁争用，剩余来自 `ThreadPool::enqueue→cond_signal` 与连接表。
   - 建议：① 线程池**批量唤醒**（`enqueue_batch` + 一次 `notify_all`）；② `get_conn` 连接表哈希分片；③ 三实例时调低 `thread_pool_size` 匹配 8 核。

3. **时间读取（~6.5%）**：`steady_clock::now` + `clock_gettime`（缓存过期判定、日志时间、超时）。
   - 建议：缓存过期判定放宽精度 / 合并时间戳读取。

4. **响应序列化（8.4%）**：`serialize` 整串拼接。
   - 建议：`writev` 拆分 headers/body 发送，省一次整串拷贝。

---

## 5. 复现方法

```bash
# 1. 以帧指针构建（否则 fp 调用链挂起/无调用栈）
make release CXXFLAGS_RELEASE="-O2 -DNDEBUG -fno-omit-frame-pointer"
./webserver --port 8081 --config /tmp/bench_single.json &

# 2. 经 Nginx 压测（后台）
wrk -t4 -c64 -d40s http://127.0.0.1/api/shops &

# 3. 采集（帧指针 + 调用链）
perf record -F 99 -g -e cpu-clock:u -p $(pgrep -f 'webserver --port 8081' | head -1) -o /tmp/perf.data -- sleep 30

# 4. 分析（--no-inline 规避挂起）
perf report -i /tmp/perf.data --stdio --no-inline -s symbol,comm
perf script -i /tmp/perf.data --demangle --no-inline -f | FlameGraph/stackcollapse-perf.pl > /tmp/perf.folded
FlameGraph/flamegraph.pl /tmp/perf.folded > perf.svg
```

---

## 6. 已知限制

- ~32% 样本落在 `[unknown]`（syscall 边界/栈损坏），火焰图已剔除；权威占比以 `perf report` Children 为准。
- `cpu-clock:u` 仅用户态；内核态 syscall 时间（%system ~165%）未计入，需配合压测资源采样（见单实例报告 §4）。
- 帧指针构建与正式发布构建存在微小差异，发布版保持原构建。

---

*报告由 GitHub Copilot 生成，基于 2026-08-15 实测 perf 数据（优化后）。*
