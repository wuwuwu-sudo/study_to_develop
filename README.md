外卖平台的雏形

经过重构，商家和顾客接发的HTTP响应都是JSON格式，且都是UTF-8的编码，更规范更安全。


菜品编辑功能有严重问题（顺便把菜品添加功能和数据库中的价格单位改了）
ctrl + shift + I开console（前端检查）


已经安装好Nginx（需要创建 Nginx 静态文件目录，日志目录和配置文件以及对webserver相对应的修改）
🛠️ 技术栈推荐
1. 基础框架层
组件	推荐技术	说明
Web框架	自建 (epoll + 线程池)	轻量级，完全控制
异步框架	libuv / boost.asio	跨平台异步I/O
协程	libco / folly	轻量级并发
序列化	RapidJSON / simdjson	高性能JSON解析
2. 网络层
组件	推荐技术	说明
反向代理	Nginx	静态文件、负载均衡
HTTP/2	nghttp2	多路复用
WebSocket	libwebsockets	实时通信
零拷贝	sendfile / splice	减少内存拷贝
3. 缓存层
组件	推荐技术	说明
内存缓存	LRU Cache (自建)	L1 缓存，微秒级
分布式缓存	Redis 7.x	L2 缓存，毫秒级
本地缓存	Caffeine (Java风格)	自实现
4. 数据库层
组件	推荐技术	说明
OLTP	PostgreSQL 16	高并发，功能丰富
嵌入式	SQLite 3.45	单机，轻量级
连接池	libpqxx / sqlite3_pool	减少连接开销
ORM	sqlpp11 / ODB	类型安全SQL
5. 监控与观测
组件	推荐技术	说明
日志	spdlog	高性能异步日志
指标	Prometheus + cpp_client	性能监控
追踪	Jaeger	分布式追踪
火焰图	perf + FlameGraph	CPU分析
6. 开发工具
组件	推荐技术	说明
构建	CMake + Ninja	快速编译
测试	Google Test	单元测试
压测	wrk / ab / vegeta	性能测试
代码质量	clang-format / clang-tidy	代码规范


                分层架构（除了依赖注入）的完整体系
┌────────────────────────────────────────────────────────┐
│  1. 接口隔离       → 精准的层间契约                    │
│  2. 分层调用规则   → 强制单向依赖，无循环              │
│  3. DTO 隔离       → 不暴露领域内部结构                │
│  4. 工厂/容器      → 自动管理对象生命周期              │
│  5. 统一异常处理   → 崩溃隔离在底层，上层返回状态码    │
│  6. 配置与策略     → 运行时动态切换实现                │
│  7. 日志与监控     → 能感知底层崩溃，快速定位          │
│  8. 领域事件       → 跨层协作时进一步解耦              │
│  9. 单元测试框架   → 验证每一层逻辑                    │
└────────────────────────────────────────────────────────┘

架构图
src/
├── main.cpp                                          # 入口：装配各层/路由/中间件，启动 epoll 事件循环
│
├── presentation/                                     # 表现层
│   ├── handlers/                                     # 请求处理器
│   │   ├── auth_handler.{h,cpp}                      # 认证/用户/商家/店铺 API
│   │   ├── dish_handler.{h,cpp}                      # 菜品 API
│   │   └── order_handler.{h,cpp}                     # 订单 API
│   ├── http/                                         # HTTP 通信模块
│   │   ├── http_server.{h,cpp}                       # epoll+线程池+keep-alive+中间件管线+writev 发送
│   │   ├── http_router.{h,cpp}                       # 路由分发（精确 + 通配符）
│   │   ├── http_parser.{h,cpp}                       # HTTP 解析（parse_one：支持半包/管道）
│   │   ├── http_request.{h,cpp}                      # 请求封装（header 大小写不敏感查询）
│   │   └── http_response.{h,cpp}                     # 响应封装（serialize_header_into + writev 拆分头/体）
│   └── dto/                                          # 数据传输对象
│       ├── user_dto.h
│       ├── dish_dto.h
│       └── order_dto.h
│
├── middleware/                                       # 中间件层（同步管线，位于路由前）
│   ├── middleware.{h,cpp}                            # Middleware 基类 + MiddlewarePipeline
│   ├── logging_middleware.{h,cpp}                    # 日志中间件（异步 Logger）
│   ├── rate_limit_middleware.{h,cpp}                 # 限流中间件（时间窗口 + 封禁）
│   └── auth_middleware.{h,cpp}                       # 认证中间件（会话校验 → 401）
│
├── application/                                      # 应用层
│   ├── auth_service.{h,cpp}                          # 认证/商家服务（开业列表三级缓存）
│   ├── dish_service.{h,cpp}                          # 菜品服务（菜品列表三级缓存）
│   └── order_service.{h,cpp}                         # 订单服务（状态机流转）
│
├── domain/                                           # 领域层
│   ├── models/
│   │   ├── user.{h,cpp}                              # 用户聚合根
│   │   ├── merchant.{h,cpp}                          # 商家聚合根
│   │   ├── dish.{h,cpp}                              # 菜品实体
│   │   ├── order.{h,cpp}                             # 订单聚合根
│   │   └── order_item.{h,cpp}                        # 订单项
│   ├── value_objects/
│   │   ├── money.{h,cpp}                             # 金额值对象
│   │   ├── address.{h,cpp}                           # 地址值对象
│   │   ├── order_status.h                            # 订单状态枚举
│   │   └── session_id.h                              # 会话 ID 值对象
│   └── services/
│       ├── price_calculator.{h,cpp}                  # 价格计算（小计/折扣/配送费）
│       └── order_status_machine.{h,cpp}              # 订单状态机（合法流转校验）
│
├── infrastructure/                                   # 基础设施层
│   ├── repositories/
│   │   ├── interfaces/
│   │   │   ├── i_user_repository.h                   # 用户仓储接口
│   │   │   ├── i_merchant_repository.h               # 商家仓储接口
│   │   │   ├── i_dish_repository.h                   # 菜品仓储接口
│   │   │   └── i_order_repository.h                  # 订单仓储接口
│   │   └── sqlite_*_repository.{h,cpp}               # SQLite 仓储实现（用户/商家/菜品/订单）
│   ├── database/
│   │   ├── db_manager.{h,cpp}                        # 数据库管理器（连接池生命周期）
│   │   ├── connection_pool.{h,cpp}                   # 连接池（PRAGMA foreign_keys / WAL）
│   │   ├── db_connection.{h,cpp}                     # SQLite 连接封装
│   │   └── migrations/
│   │       ├── migration.h                           # 迁移接口
│   │       └── init_db.{h,cpp}                       # 建表迁移（含旧库 ALTER）
│   ├── cache/
│   │   ├── local_cache.{h,cpp}                       # L1 本地内存缓存（16 分片 + TTL）
│   │   ├── redis_client.{h,cpp}                      # L2 Redis 客户端（自实现 RESP，故障降级）
│   │   └── multi_level_cache.{h,cpp}                 # 三级缓存编排 L1→L2→L3(loader)
│   ├── session/
│   │   ├── session_manager.{h,cpp}                   # 会话管理器（创建/校验/清理线程）
│   │   └── session_store.{h,cpp}                     # 会话存储（InMemory 内存版）
│   └── common/
│       ├── logger.{h,cpp}                            # 异步批量日志
│       ├── config.{h,cpp}                            # key=value 配置加载
│       └── exception.{h,cpp}                         # 统一异常 AppException
│
├── shared/                                           # 共享层（无业务依赖，被各层复用）
│   ├── constants.h                                   # 常量
│   ├── enums.h                                       # 全局枚举（HttpMethod / UserRole …）
│   ├── utils.{h,cpp}                                 # 工具（密码哈希 / trim / url_decode …）
│   ├── thread_pool.{h,cpp}                           # 线程池（事件循环 offload 业务处理）
│   ├── delayed_shrink.h                              # 延迟缩容策略（复用缓冲防峰值内存）
│   ├── length_prefixed_framer.{h,cpp}                # 长度前缀分帧协议（自定义二进制协议）
│   └── config.json                                   # 服务器默认配置
│
└── tests/                                            # 单元测试（tests/，独立构建，见下方）

架构总览
┌─────────────────────────────────────────────────────────────────────────────┐
│                         完整系统架构（请求自上而下流经各层）                  │
│                                                                             │
│  ┌─────────────────────────────────────────────────────────────────────┐   │
│  │                     表现层 Presentation Layer                       │   │
│  │   Handlers(处理器)  ·  HTTP(服务器/路由/解析/请求/响应)  ·  DTO       │   │
│  │   服务器 = epoll 事件循环 + 线程池 + keep-alive + 中间件管线 + writev  │   │
│  └─────────────────────────────────────────────────────────────────────┘   │
│      │  中间件（路由前）：Logging → RateLimit → Auth                        │
│      ▼                                                                     │
│  ┌─────────────────────────────────────────────────────────────────────┐   │
│  │                      应用层 Application Layer                       │   │
│  │   AuthService · DishService · OrderService（服务编排）                │   │
│  │   读路径查多级缓存（L1→L2→L3），命中零 DB；写后按前缀失效             │   │
│  └─────────────────────────────────────────────────────────────────────┘   │
│      ▼                                                                     │
│  ┌─────────────────────────────────────────────────────────────────────┐   │
│  │                      领域层 Domain Layer                            │   │
│  │   Models(聚合根) · Value Objects · Services(计价/订单状态机)         │   │
│  │   业务规则集中于此，不依赖基础设施                                    │   │
│  └─────────────────────────────────────────────────────────────────────┘   │
│      ▼                                                                     │
│  ┌─────────────────────────────────────────────────────────────────────┐   │
│  │                   基础设施层 Infrastructure Layer                   │   │
│  │   Repositories · Database(连接池) · Cache(多级) · Session · Common   │   │
│  │   依赖倒置：上层只依赖接口，具体实现可替换                           │   │
│  └─────────────────────────────────────────────────────────────────────┘   │
│      ▲  共享层 Shared（无业务依赖，被各层复用）                              │
│      │   ThreadPool · DelayedShrink · LengthPrefixedFramer · Utils/Constants│
│      ▼                                                                     │
│  外部依赖：SQLite(经连接池) · Redis(可选 L2) · Nginx(可选反向代理/静态文件)  │
└─────────────────────────────────────────────────────────────────────────────┘

数据流向：
┌─────────────────────────────────────────────────────────────────────────────┐
│                         请求处理数据流（读路径为例）                         │
│                                                                             │
│  客户端 HTTP 请求（keep-alive 长连接）                                       │
│      │                                                                      │
│      ▼                                                                      │
│  ┌─────────────────────────────────────────────────────────────────────┐   │
│  │ 1. HttpServer 接收（事件循环 + 线程池）                              │   │
│  │    ├─ epoll 就绪事件 → 提交线程池（捕获 shared_ptr 防 fd 复用竞态）    │   │
│  │    └─ process_client：非阻塞 recv → parse_one 解析（支持半包/管道）    │   │
│  └─────────────────────────────────────────────────────────────────────┘   │
│      │                                                                      │
│      ▼                                                                      │
│  ┌─────────────────────────────────────────────────────────────────────┐   │
│  │ 2. 中间件管线 MiddlewarePipeline（同步 next 链）                     │   │
│  │    ├─ LoggingMiddleware：记录 method/path/status/耗时（异步批量日志）  │   │
│  │    ├─ RateLimitMiddleware：按客户端 key 时间窗口限流，超限封禁 → 429   │   │
│  │    └─ AuthMiddleware：受保护路径校验会话，无效 → 401（不注入上下文）   │   │
│  └─────────────────────────────────────────────────────────────────────┘   │
│      │                                                                      │
│      ▼                                                                      │
│  ┌─────────────────────────────────────────────────────────────────────┐   │
│  │ 3. HttpRouter 路由分发（精确 + 通配符）→ 调用对应 Handler             │   │
│  └─────────────────────────────────────────────────────────────────────┘   │
│      │                                                                      │
│      ▼                                                                      │
│  ┌─────────────────────────────────────────────────────────────────────┐   │
│  │ 4. Handler（表现层）                                                 │   │
│  │    ├─ 解析参数（query / body JSON）并校验，读取会话                   │   │
│  │    ├─ 调用 Service；AppException → 状态码映射（400/401/404/409/500）  │   │
│  │    └─ 结果 DTO → JSON 响应                                           │   │
│  └─────────────────────────────────────────────────────────────────────┘   │
│      │                                                                      │
│      ▼                                                                      │
│  ┌─────────────────────────────────────────────────────────────────────┐   │
│  │ 5. Service（应用层）编排                                             │   │
│  │    ├─ 读：查 MultiLevelCache（L1 本地 → L2 Redis → L3 数据源回填）     │   │
│  │    ├─ 缓存命中 → 直接返回序列化 JSON（零 DB、零序列化）                │   │
│  │    ├─ 未命中 → 调领域对象 + 仓储                                       │   │
│  │    └─ 写：执行后按商家/菜品前缀失效两级缓存                            │   │
│  └─────────────────────────────────────────────────────────────────────┘   │
│      │                                                                      │
│      ▼                                                                      │
│  ┌─────────────────────────────────────────────────────────────────────┐   │
│  │ 6. Domain（领域层）执行业务规则                                       │   │
│  │    ├─ 构建/修改聚合根并 validate()（User/Merchant/Dish/Order）         │   │
│  │    └─ 领域服务：PriceCalculator 计价 · OrderStatusMachine 状态流转     │   │
│  └─────────────────────────────────────────────────────────────────────┘   │
│      │                                                                      │
│      ▼                                                                      │
│  ┌─────────────────────────────────────────────────────────────────────┐   │
│  │ 7. Repository（基础设施层）持久化                                     │   │
│  │    ├─ DbManager 连接池取 SQLite 连接（PRAGMA foreign_keys / WAL）      │   │
│  │    └─ 领域对象 ↔ SQL（绑定真实字段）；会话走 SessionManager（内存）     │   │
│  └─────────────────────────────────────────────────────────────────────┘   │
│      │                                                                      │
│      ▼                                                                      │
│  ┌─────────────────────────────────────────────────────────────────────┐   │
│  │ 8. 响应返回                                                          │   │
│  │    ├─ 缓存命中：缓存 JSON 直接作响应体（零序列化）                     │   │
│  │    ├─ HttpResponse：serialize_header_into + writev 拆分头/体（零拷贝） │   │
│  │    ├─ 线程级缓冲复用 + 延迟缩容（防峰值内存）                         │   │
│  │    └─ keep-alive 判定：长连接等待下一请求，或关闭                     │   │
│  └─────────────────────────────────────────────────────────────────────┘   │
│      │                                                                      │
│      ▼                                                                      │
│  客户端收到 HTTP 响应                                                        │
└─────────────────────────────────────────────────────────────────────────────┘

项目测试文件目录的结构
test/
├── CMakeLists.txt                          # 测试构建配置
├── main_test.cpp                           # 测试主入口（可选）
│
├── unit/                                   # 单元测试
│   ├── presentation/
│   │   ├── auth_handler_test.cpp
│   │   ├── dish_handler_test.cpp
│   │   └── order_handler_test.cpp
│   │
│   ├── application/
│   │   ├── auth_service_test.cpp
│   │   ├── dish_service_test.cpp
│   │   └── order_service_test.cpp
│   │
│   ├── domain/
│   │   ├── models/
│   │   │   ├── user_test.cpp
│   │   │   ├── merchant_test.cpp
│   │   │   ├── dish_test.cpp
│   │   │   ├── order_test.cpp
│   │   │   └── order_item_test.cpp
│   │   │
│   │   └── value_objects/
│   │       ├── money_test.cpp
│   │       ├── address_test.cpp
│   │       └── order_status_test.cpp
│   │
│   └── infrastructure/
│       ├── repositories/
│       │   ├── sqlite_user_repository_test.cpp
│       │   └── sqlite_order_repository_test.cpp
│       └── session/
│           └── session_manager_test.cpp
│
├── integration/                            # 集成测试
│   ├── auth_integration_test.cpp
│   ├── dish_integration_test.cpp
│   ├── order_integration_test.cpp
│   └── database_integration_test.cpp
│
├── mock/                                   # Mock 对象（头文件）
│   ├── mock_user_repository.h
│   ├── mock_merchant_repository.h
│   ├── mock_dish_repository.h
│   ├── mock_order_repository.h
│   └── mock_session_manager.h
│
├── fixture/                                # 测试夹具
│   ├── test_database.h
│   ├── test_database.cpp
│   ├── test_data.h
│   ├── test_data.cpp
│   └── test_utils.h
│
└── performance/                            # 性能测试（可选）
    ├── auth_performance_test.cpp
    └── database_performance_test.cpp

---

## 🚀 三实例反向代理部署（Nginx 负载均衡）

```
客户端
  │
  ▼
Nginx (:80)  ── 静态文件直出 (www/)
  │
  └── /api/* ──► upstream webserver_backend (ip_hash)
                   ├── instance1 127.0.0.1:8081
                   ├── instance2 127.0.0.1:8082
                   └── instance3 127.0.0.1:8083
```

### 部署步骤
1. 构建：`make release`
2. 启动三实例：`./start_servers.sh`（停止：`./stop_servers.sh`）
3. 部署 Nginx：`sudo ./deploy_nginx.sh`（复制配置、拷贝静态文件、reload）

### 配置文件
| 文件 | 说明 |
|------|------|
| `nginx/food_delivery.conf` | Nginx 反向代理配置（三实例 upstream、ip_hash、限流、健康检查） |
| `config/instance_8081.json` | 实例1 配置（端口 8081，`--port` 命令行指定） |
| `config/instance_8082.json` | 实例2 配置（端口 8082） |
| `config/instance_8083.json` | 实例3 配置（端口 8083） |
| `start_servers.sh` / `stop_servers.sh` | 三实例启停脚本（PID 文件在 `logs/`） |

### ⚠️ 关键注意事项（依据 webserver 实现）
- **会话一致性**：会话是进程内内存存储（`InMemorySessionStore`），不跨实例共享。
  Nginx 已用 `ip_hash` 保证同一客户端 IP 固定命中同一实例；若某实例宕机被摘除，
  该 IP 的会话会暂时失效（登录态丢失），故障恢复后自动恢复。
- **真实 IP 透传**：限流中间件按 `X-Real-IP` / `X-Forwarded-For` 识别客户端，
  Nginx 已透传；若不走 Nginx 直连后端，限流会按 `127.0.0.1` 计算。
- **SQLite 写锁**：三实例共享同一 `food_delivery.db`（`busy_timeout=5000ms`），
  SQLite 为单写者模型，高并发写入可能出现 `SQLITE_BUSY`。写放大场景建议：
  开启 WAL、或迁移 PostgreSQL/MySQL、或改用 Redis 做跨实例会话。
- **Redis 缓存**：L2 Redis 由三实例共享，作为跨实例统一缓存层，建议保持开启。

### 验证
```bash
./start_servers.sh
curl http://127.0.0.1:8081/health    # 各实例直连
curl http://localhost/health         # 经 Nginx
curl http://localhost/api/shops      # 经 Nginx 负载均衡
tail -f /var/log/nginx/food_delivery/access.log
```
