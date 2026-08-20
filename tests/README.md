# 单元测试（Unit Tests）

本目录为 `src/` 各模块编写了对应的单元测试，测试结构镜像 `src/` 的分层。

## 运行方式

### Linux / macOS（Bash）

要求：`g++`（可用环境变量 `CXX` 覆盖，如 `CXX=clang++`）。

```bash
bash tests/run_tests.sh
# 或先赋予执行权限后直接运行：
chmod +x tests/run_tests.sh
./tests/run_tests.sh
```

### Windows（PowerShell / 命令提示符）

要求：MinGW g++（默认路径 `D:\MinDW w64\mingw64\bin\g++.exe`，可用环境变量 `CXX` 或 `GCC` 覆盖）。

方式一（PowerShell）：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests/build_tests.ps1
```

方式二（命令提示符 / 双击）：

```
tests\run_tests.bat
```

> `build_tests.ps1` 为跨平台脚本，也可用 PowerShell Core（`pwsh`）在 Linux 上运行；
> 在 Linux 下其等价于 `run_tests.sh`。

所有方式都会先编译、再运行测试，最后给出汇总，例如：

```
[==========] 161 tests run, 161 passed, 0 failed.
```

编译产物输出到 `tests/build/server_tests`（Linux/macOS）或 `tests/build/server_tests.exe`（Windows）。

## 目录结构

```
tests/
├── test_framework.h        # 零依赖轻量测试框架（GoogleTest 风格 API）
├── test_main.cpp           # 测试程序入口
├── build_tests.ps1         # 跨平台构建运行脚本（Windows PowerShell / Linux pwsh）
├── run_tests.bat           # Windows 命令行包装脚本
├── run_tests.sh            # Linux/macOS Bash 构建运行脚本
├── mocks/
│   └── mock_repositories.h # 仓储接口的内存版测试替身
├── domain/                 # 对应 src/domain
├── application/            # 对应 src/application（配合 mock 仓储）
├── infrastructure/         # 对应 src/infrastructure（config/exception/session）
├── presentation/           # 对应 src/presentation（http/dto/handlers）
└── middleware/             # 对应 src/middleware
```

## 覆盖范围

测试覆盖**不依赖 OpenSSL / SQLite / POSIX** 的纯逻辑模块：

- 领域模型：`Money`、`Address`、`OrderItem`、`Order`、`User`、`Merchant`、`Dish`
- 领域服务：`PriceCalculator`、`OrderStatusMachine`
- 基础设施：`Config`、`Exception`、`SessionStore` / `SessionManager`
- 应用层：`AuthService`、`DishService`、`OrderService`（配合 mock 仓储）
- 表现层：`HttpRequest`、`HttpResponse`、`HttpParser`、`HttpRouter`
- 中间件：`MiddlewarePipeline`、`AuthMiddleware`、`RateLimitMiddleware`、`LoggingMiddleware`
- 处理器：`AuthHandler`、`DishHandler`、`OrderHandler`

未覆盖（需要 OpenSSL / SQLite / POSIX，无法在当前环境直接编译）：

- `shared/utils.cpp`（依赖 `<openssl/sha.h>`）
- `infrastructure/database/*`、`infrastructure/repositories/sqlite_*`
- `presentation/http/http_server.cpp`（依赖 epoll / POSIX socket）
- `main.cpp`

## 注意事项

- 应用层服务与处理器中有大量 `TODO` 占位实现，相关测试断言的是**当前**行为
  （例如 `login_user` 返回占位会话、handler 返回 501），待实现真实逻辑后需同步更新。
- `Config` 是单例且无法清空，`Config.UseDefaultsFillsMissing` 必须最先运行
  （见 `test_config.cpp` 顶部注释）。
- 测试发现并修复了 `src/presentation/http/http_parser.cpp` 的一个 bug：
  原代码用 `line != "\r\n"` 判断头部结束，但 `std::getline` 读到的是 `"\r"`，
  导致请求体（POST body）不会被解析；已改为「剥离 `\r` 后遇到空行即结束」。

后期无需更改的测试文件：
#	测试文件	                        依赖源码	                            核对结果
1	test_money.cpp	                    money.cpp	                            ✅ 完整
2	test_address.cpp	                address.cpp	                            ✅ 完整
3	test_order_item.cpp	                order_item.cpp	                        ✅ 完整
4	test_user.cpp	                    user.cpp	                            ✅ 完整
5	test_merchant.cpp	                merchant.cpp	                        ✅ 完整
6	test_order_status_machine.cpp	    order_status_machine.cpp	✅ 完整（终态返回 false 是真实规则，非占位）
7	test_exception.cpp	                exception.cpp	                        ✅ 完整
8	test_config.cpp	                    config.cpp	                            ✅ 完整
9	test_session.cpp	                session_store.cpp / session_manager.cpp	✅ 完整（cleanup_expired 的 TODO 不被测试依赖）
10	test_http_request.cpp	            http_request.cpp	                    ✅ 完整
11	test_http_response.cpp	            http_response.cpp	                    ✅ 完整
12	test_http_parser.cpp	            http_parser.cpp	                        ✅ 完整（bug 已修复，测试通过）
13	test_http_router.cpp	            http_router.cpp	                        ✅ 完整
14	test_middleware.cpp	                middleware / auth / rate_limit / logging	✅ 完整