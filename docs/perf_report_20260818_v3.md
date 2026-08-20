# perf 性能剖析报告 v3：webserver 单实例（tp=3 + warn 日志 + 批量读发 + 批量唤醒后）

- **测试日期**：2026-08-18
- **架构**：wrk → **webserver(8081 单实例)**（直连；`thread_pool_size=3`、`log_level=warn`、高限流）
- **工具**：perf 7.0.12 + FlameGraph（`-F 99 -g -e cpu-clock:u`，30s，c=64 负载）
- **被测**：release 构建（帧指针 `-fno-omit-frame-pointer -g`，C++17, epoll + **3 线程池（批量唤醒）** + SQLite WAL）
- **本版已应用优化**（相对 8/17 v2/v3 报告）：
  ① 日志时间戳每秒缓存；② LocalCache 16 分片锁 + 分片熔断器；③ writev 拆分响应头/体；④ thread_local 复用；⑤ 延迟缩容；⑥ 菜品分页字符串直出（方案 A）；⑦ 对象池/内存池移除；⑧ 日志级别下调 + INFO 抽样；⑨ 线程池 3；⑩ **线程池批量唤醒**（任务数达阈值 notify_all 全员开工、少量 0.5ms 攒批）；⑪ **批量读 + 批量发 + 超时兜底**（读 50ms/256KB 兜底，整批响应 `send_buf` 一次 writev）；⑫ 请求保护器（有界队列+熔断+高水位降级）；⑬ 降级/熔断状态日志（采样）；⑭ 日志 500ms 批量落盘；⑮ **`parse_int_or_default`（from_chars）修复 stoi 空串异常**
- **本次剖析亮点**：**确认 8/17 v3 定位的 `/api/dishes` `std::stoi("")` 空串异常热点（~13.5B libgcc）已彻底消除**（libgcc/_Unwind 分类双接口均为 0.0B）；**新浮现热点为批量发 `basic_string::_M_append`（send_buf 拼接）**
- **采样**：`/api/shops` 0.373 MB（QPS 108.8k）、`/api/dishes` 0.405 MB（QPS 86.6k）
- **产物**：`docs/perf_flamegraph_20260818_v3_shops.svg`、`docs/perf_flamegraph_20260818_v3_dishes.svg`

---

## 1. 优化效果总览（对比 8/17 v2/v3 剖析）

| 指标 | 8/17 v3（shops） | **8/18 v3（shops）** | 变化 |
|------|------------------|----------------------|------|
| 异常/栈展开（libgcc/_Unwind/_gxx/_dl_find） | 少 | **0.0B** | 无异常热点 |
| 网络 I/O（recv/writev/memmove） | ~7.4B（首位） | **~6.6B（首位）** | 保持主导 |
| 请求解析（parse_one + istringstream） | ~1.6B | **~3.7B** | 高 QPS 下占比上升 |
| 字符串拼接（basic_string::_M_append） | 未单列 | **~4.8B（新热点）** | 批量发 send_buf 拼接 |
| 内存分配/释放（malloc/cfree） | ~1.2B | **~1.9B** | 仍非首位 |

> **核心结论**：
> 1. **`std::stoi("")` 空串异常已根治**（8/17 v3 dishes ~13.5B libgcc → 8/18 0.0B），`parse_int_or_default`（from_chars）生效。
> 2. **网络 I/O 仍是用户态主导**；**批量发 `send_buf` 拼接（`_M_append`）成为新浮现热点**（shops 4.8B / dishes 6.5B）——这是批量发"省 writev 次数"与"每响应 body 拷贝"权衡的剖析体现。

---

## 2. self 热点表（8/18 v3，两接口对照）

### 2.1 `GET /api/shops`（字符串直出缓存，用户态采样 0.373 MB，QPS 108.8k）

| Self 计数 | 函数 | 归属 |
|------:|------|------|
| 3.33B | `__libc_recv` | 网络接收 |
| 2.20B | `__GI___writev` | 网络发送（批量发一次 writev） |
| 2.00B | `pthread_mutex_lock` | 锁（连接表/线程池/保护器） |
| 1.18B | `[[vdso]]` | 系统调用/时钟（gettimeofday/clock_gettime） |
| 1.11B | `__memmove_evex_unaligned_erms` | 内存拷贝 |
| 1.03B | `pthread_mutex_unlock` | 锁 |
| 0.94B | `MiddlewarePipeline::execute` | 中间件链（栈上 ChainState） |
| 0.86B | `basic_string::_M_append` | **批量发 send_buf 拼接** |
| 0.78B | `std::_Function_handler` | 闭包调用 |
| 0.78B | `__GI___lll_lock_wake` | 锁唤醒 |
| 0.73B | `malloc` | 内存分配 |
| 0.60B | `HttpServer::process_client` | 请求处理 |
| 0.55B | `HttpParser::parse_one` | 请求解析 |
| 0.54B | `_int_free` | 内存释放 |
| 0.47B | `LoggingMiddleware::handle` | 中间件（warn 下基本空转） |

### 2.2 `GET /api/dishes?merchant_id=1`（直出缓存 + 分页，用户态采样 0.405 MB，QPS 86.6k）

| Self 计数 | 函数 | 归属 |
|------:|------|------|
| 2.82B | `__libc_recv` | 网络接收 |
| 1.86B | `__GI___writev` | 网络发送（批量发） |
| 1.74B | `pthread_mutex_lock` | 锁 |
| 1.23B | `pthread_mutex_unlock` | 锁 |
| 1.23B | `__memmove_evex_unaligned_erms` | 内存拷贝 |
| 1.09B | `[[vdso]]` | 系统调用/时钟 |
| 1.09B | `basic_string::_M_append` | **批量发 send_buf 拼接** |
| 0.88B | `MiddlewarePipeline::execute` | 中间件链 |
| 0.83B | `HttpParser::parse_one` | 请求解析 |
| 0.73B | `malloc` | 内存分配 |
| 0.67B | `__GI___lll_lock_wake` | 锁唤醒 |
| 0.64B | `HttpServer::process_client` | 请求处理 |
| 0.56B | `cfree` | 内存释放 |
| 0.54B | `std::_Function_handler` | 闭包调用 |
| 0.47B | `_int_free` | 内存释放 |

> **8/17 v3 的 libgcc 异常热点（13.5B）已从 dishes 表中完全消失**；网络 I/O + 锁主导，批量发拼接（`_M_append`）为新增。

---

## 3. 分类聚合与热点对比（8/18 v3）

| 类别 | `/api/shops` | `/api/dishes` | 说明 |
|------|-------------|---------------|------|
| **网络 I/O（recv/writev/memmove）** | **~6.6B（首位）** | **~5.9B（首位）** | 双接口主导 |
| **字符串拼接（_M_append/basic_string）** | **~4.8B** | **~6.5B** | 批量发 send_buf 拼接（dishes body 更大） |
| **锁/条件变量（mutex/lll_lock_wake）** | ~4.4B（含 vdso） | ~4.1B | 连接表/线程池/保护器 |
| **请求解析（parse_one + istringstream）** | ~3.7B | ~5.4B | 高 QPS 下占比上升 |
| **内存分配/释放（malloc/cfree）** | ~1.9B | ~1.8B | 已非热点 |
| **异常/栈展开（libgcc/_Unwind/_gxx）** | **0.0B** | **0.0B** | **stoi 修复生效，完全消除** |

### 🎯 关键验证：8/17 v3 的 dishes 异常热点已根治

8/17 v3 报告定位 `/api/dishes` 每请求 2 次 `std::stoi("")` 空串异常（`page`/`page_size` 缺失）→ `__cxa_throw` → `_Unwind_RaiseException` → libgcc ~13.5B。本次已用 `parse_int_or_default`（`std::from_chars`，不抛异常）替换，**libgcc/_Unwind/_gxx/_dl_find 分类双接口均 0.0B**——修复完整生效，异常栈展开成本归零。

---

## 4. 剩余瓶颈与下一步优化（按收益排序）

1. **网络 I/O（recv/writev，双接口首位 ~6.6B/5.9B）**：系统态 %system ~79% 也是网络 syscall 主导。
   - 已落地：日志级别下调（warn）、批量读/发；网络侧依赖 Nginx `sendfile`/`open_file_cache`（已部署）。
   - 建议：连接级 `sendmsg` 多缓冲批量；静态页依赖 Nginx sendfile（批量发对 send_file 大 body 有拼接开销）。

2. **批量发 `basic_string::_M_append`（新热点，shops 4.8B / dishes 6.5B）**：`send_buf` 拼接引入每响应 header+body 两次 append（body 一次 memcpy）。
   - 这是批量发"省 writev 次数"的代价（trade-off）；对 /api/shops、/api/dishes 小 body 影响有限，静态页/大 body 场景建议走 Nginx sendfile。
   - 可选优化：`send_buf` 用 `reserve` 预扩容量减少 realloc；或仅管道多响应时走批量、单响应走头/体 iovec 零拷贝。

3. **请求解析 `parse_one` + `istringstream operator>>`（shops 3.7B / dishes 5.4B）**：每请求构造 stringstream/locale/ios 有开销。
   - 建议：`parse_one` 请求行改 `string_view` 手工解析（8/15 v2、8/17 v3 已建议，仍未做），省 ~8-10% 请求解析成本。

4. **锁/条件变量（shops ~4.4B / dishes ~4.1B 含 vdso）**：线程池批量唤醒已减少 enqueue 信号，但连接表互斥 + RequestGuard（usage_ratio/breaker 锁）仍在。
   - 建议：连接表哈希分片；RequestGuard 熔断器已在 LocalCache 分片隔离。

5. **内存分配（~1.9B，已非热点）**：thread_local 复用 + 直出已把每请求分配压到最低；不要再引入全局分配器替换（SlabPool/对象池实测中性或回归，已移除）。

---

## 5. 复现方法

```bash
# 1. 帧指针构建（剖析后恢复正式构建：make -B release）
make -B release CXXFLAGS_RELEASE="-O2 -DNDEBUG -fno-omit-frame-pointer -g"

# 2. 启动单实例（tp=3 + log_level=warn + 高限流；stdout /dev/null 防日志）
sed -e 's/server.rate_limit.max_requests=100/server.rate_limit.max_requests=10000000/' \
    -e 's/server.log_level=info/server.log_level=warn/' \
    config/instance_8081.json > /tmp/perf_bench.json
./webserver --port 8081 --config /tmp/perf_bench.json &

# 3. 压测 + 采集（30s 采样，c=64；需先 sudo sysctl kernel.perf_event_paranoid=1）
WPID=$(pgrep -f 'webserver --port 8081' | head -1)
wrk -t4 -c64 -d40s http://127.0.0.1:8081/api/shops &
perf record -F 99 -g -e cpu-clock:u --no-buildid -p "$WPID" -o /tmp/perf_shops.data -- sleep 30

# 4. 折叠 + 火焰图
perf script -i /tmp/perf_shops.data --demangle --no-inline -f | FlameGraph/stackcollapse-perf.pl > /tmp/perf_shops.folded
FlameGraph/flamegraph.pl /tmp/perf_shops.folded > docs/perf_flamegraph_20260818_v3_shops.svg

# 5. 热点统计（count 为行尾数字，函数名含空格勿按空白字段解析）
awk '{c=$NF; sub(/[0-9]+$/,"",$0); sub(/ +$/,"",$0); n=split($0,a,";"); self[a[n]]+=c}
     END{for(k in self) printf "%8d %s\n",self[k],k}' /tmp/perf_shops.folded | sort -rn | head -15
```

---

## 6. 已知限制

- `cpu-clock:u` 仅用户态；内核态 syscall 时间（%system ~79%）未计入，需配合压测资源采样（见三实例报告 §4）。
- 帧指针构建与正式发布构建存在微小差异，发布版保持原构建。
- 本报告为**直连 8081** 采集（未走 Nginx），与 8/17 v2 的"经 Nginx"架构不同，绝对值不可直接对比，但热点分布可参考。
- 本版与 8/17 v3 的差异还包括批量唤醒/批量读发/保护器等优化，热点分布变化由多优化叠加所致，非单一因素。

---

*报告由 GitHub Copilot 生成，基于 2026-08-18 v3 实测 perf 数据（tp=3 + warn 日志 + 批量唤醒 + 批量读发后；shops/dishes 双接口对照；确认 stoi 空串异常已根治，新热点为批量发 send_buf 拼接）。*
