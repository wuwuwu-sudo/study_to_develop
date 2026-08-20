// ============================================================
// src/main.cpp
// Food Delivery Server - 主入口
// ============================================================

#include <charconv>
#include <iostream>
#include <memory>
#include <signal.h>
#include <system_error>
#include <unistd.h>

// ============================================================
// 表现层
// ============================================================
#include "presentation/http/http_server.h"
#include "presentation/http/http_router.h"
#include "presentation/handlers/auth_handler.h"
#include "presentation/handlers/dish_handler.h"
#include "presentation/handlers/order_handler.h"

// ============================================================
// 中间件层
// ============================================================
#include "middleware/logging_middleware.h"
#include "middleware/auth_middleware.h"
#include "middleware/rate_limit_middleware.h"

// ============================================================
// 应用层
// ============================================================
#include "application/auth_service.h"
#include "application/dish_service.h"
#include "application/order_service.h"

// ============================================================
// 基础设施层
// ============================================================
#include "infrastructure/database/db_manager.h"
#include "infrastructure/database/connection_pool.h"   
#include "infrastructure/database/migrations/init_db.h"
#include "infrastructure/repositories/sqlite_user_repository.h"
#include "infrastructure/repositories/sqlite_merchant_repository.h"
#include "infrastructure/repositories/sqlite_dish_repository.h"
#include "infrastructure/repositories/sqlite_order_repository.h"
#include "infrastructure/session/session_manager.h"
#include "infrastructure/cache/local_cache.h"
#include "infrastructure/cache/redis_client.h"
#include "infrastructure/cache/multi_level_cache.h"
#include "infrastructure/common/logger.h"
#include "infrastructure/common/config.h"
#include "infrastructure/common/exception.h"

// ============================================================
// 共享层
// ============================================================
#include "shared/constants.h"
#include "shared/request_guard.h"
#include "shared/utils.h"

using namespace infrastructure::common;
using namespace infrastructure::database;
using namespace infrastructure::session;
using namespace infrastructure::repositories;
using namespace application;
using namespace presentation::handlers;
using namespace presentation::http;
using namespace presentation::middleware;
using namespace constants;

// ============================================================
// 全局变量（用于信号处理）
// ============================================================
static std::unique_ptr<HttpServer> g_server;

// ============================================================
// 创建业务读路径请求保护器（有界队列 + 熔断器 + 高水位降级）
// 参数可由配置覆盖（server.queue.*），非法值回退默认：
//   server.queue.max              有界队列容量（默认 256）
//   server.queue.high_watermark   降级水位，使用率超过即触发本地缓存降级（默认 0.8）
//   server.queue.wait_timeout_ms  排队等待名额超时（默认 50ms）
//   server.queue.failure_threshold  熔断连续失败阈值（默认 5）
//   server.queue.cooldown_ms      熔断冷却（默认 1000ms）
//   server.queue.half_open_max_probes 半开探测名额（默认 1）
//   server.queue.shed_log_sample  降级执行日志抽样分母（0=关闭；1=全量；N=每 N 条记 1 条，默认 100）
//
// 日志约定：降级执行（高水位/熔断/队列超时）与熔断状态切换均输出 WARN
// 级日志（不打印 ERROR，避免误报警）；降级日志按 shed_log_sample 采样，
// 高负载下不会刷屏；状态切换为低频事件，逐条记录。
// ============================================================
namespace {

double parse_ratio(const std::string& s, double default_value) {
    if (s.empty()) {
        return default_value;
    }
    double v = default_value;
    const char* begin = s.data();
    const char* end = s.data() + s.size();
    auto [ptr, ec] = std::from_chars(begin, end, v);
    if (ec == std::errc() && ptr == end && v > 0.0 && v <= 1.0) {
        return v;
    }
    return default_value;
}

std::shared_ptr<shared::RequestGuard> make_request_guard() {
    auto& config = Config::instance();
    shared::RequestGuard::Config cfg;
    int max_queue = config.get_int("server.queue.max", 256);
    cfg.max_queue = max_queue > 0 ? static_cast<std::size_t>(max_queue) : 256;
    cfg.high_watermark =
        parse_ratio(config.get("server.queue.high_watermark", "0.8"), 0.8);
    int wait_ms = config.get_int("server.queue.wait_timeout_ms", 50);
    cfg.queue_wait_timeout =
        std::chrono::milliseconds(wait_ms > 0 ? wait_ms : 50);
    int threshold = config.get_int("server.queue.failure_threshold", 5);
    cfg.failure_threshold = threshold > 0 ? threshold : 5;
    int cooldown_ms = config.get_int("server.queue.cooldown_ms", 1000);
    cfg.cooldown = std::chrono::milliseconds(cooldown_ms > 0 ? cooldown_ms : 1000);
    int probes = config.get_int("server.queue.half_open_max_probes", 1);
    cfg.half_open_max_probes = probes > 0 ? static_cast<std::size_t>(probes) : 1;
    int shed_log_sample = config.get_int("server.queue.shed_log_sample", 100);
    cfg.shed_log_sample =
        shed_log_sample > 0 ? static_cast<std::size_t>(shed_log_sample) : 0;
    // 降级执行日志（按 shed_log_sample 采样，WARN 级，不打印 ERROR）
    cfg.on_shed_log = [](const std::string& reason) {
        LOG_WARN("request guard shed: " + reason);
    };
    // 熔断状态切换日志（低频，WARN 级）
    cfg.on_breaker_state = [](shared::CircuitState from, shared::CircuitState to) {
        LOG_WARN("request guard breaker: " + std::string(shared::to_string(from)) +
                 " -> " + shared::to_string(to));
    };
    return std::make_shared<shared::RequestGuard>(cfg);
}

}  // namespace

// ============================================================
// 信号处理函数
// 仅停止服务器（关闭 fd 使 epoll_wait 返回），让事件循环自然退出，
// 由 main() 走完整清理路径（含线程池 join），避免 exit() 与工作线程并发析构。
// ============================================================
void signal_handler(int sig) {
    LOG_INFO("Received signal: " + std::to_string(sig));
    if (g_server) {
        LOG_INFO("Shutting down server...");
        g_server->stop();
    }
}

// ============================================================
// 注册信号处理
// ============================================================
void setup_signal_handlers() {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGPIPE, SIG_IGN);  // 忽略管道破裂信号
}

// ============================================================
// 打印启动信息
// ============================================================
void print_banner(int port) {
    std::cout << R"(
╔══════════════════════════════════════════════════════════════╗
║                                                              ║
║   🍔  Food Delivery Server                                  ║
║   📦  Version 1.0.0                                         ║
║   🚀  Running on http://localhost:)" << port << R"(          ║
║                                                              ║
║   📁  Static files: ./www                                   ║
║   📊  Database: food_delivery.db                            ║
║                                                              ║
╚══════════════════════════════════════════════════════════════╝
    )" << std::endl;
}

// 主函数
// ============================================================
int main(int argc, char* argv[]) {
     try {
        // ============================================================
        // 1. 解析命令行参数
        // ============================================================
        int port = DEFAULT_PORT;
        std::string config_path = "src/shared/config.json";
        bool show_help = false;

        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--port" && i + 1 < argc) {
                port = std::stoi(argv[++i]);
            } else if (arg == "--config" && i + 1 < argc) {
                config_path = argv[++i];
            } else if (arg == "--help" || arg == "-h") {
                show_help = true;
            } else if (arg == "--test") {
                // 测试模式
                std::cout << "Running in test mode..." << std::endl;
                return 0;
            }
        }
    
         if (show_help) {
            std::cout << "Usage: ./webserver [options]\n"
                      << "Options:\n"
                      << "  --port <port>     Server port (default: 8080)\n"
                      << "  --config <path>   Config file path (default: src/shared/config.json)\n"
                      << "  --test            Run in test mode\n"
                      << "  --help, -h        Show this help\n";
            return 0;
        }

        // ============================================================
        // 2. 初始化日志系统
        // ============================================================
        std::string log_file = "server_" + std::to_string(port) + ".log";
        Logger::instance().initialize(LogLevel::INFO, log_file, true);
        LOG_INFO("========================================");
        LOG_INFO("Food Delivery Server Starting...");
        LOG_INFO("Port: " + std::to_string(port));
        LOG_INFO("Config: " + config_path);
        LOG_INFO("Log: " + log_file);
        LOG_INFO("========================================");

        
        //============================================================
        // 3. 加载配置
        // ============================================================
        auto& config = Config::instance();
        try {
            config.load(config_path);
            LOG_INFO("Config loaded from: " + config_path);
        } catch (const ConfigLoadException& e) {
            LOG_WARN("Config load failed, using defaults: " + std::string(e.what()));
            config.use_defaults();
        }
        // 日志级别下调 + INFO 级抽样（从配置读取，覆盖 initialize 时的默认 INFO/全量）：
        //   server.log_level   = debug/info/warn/error（压测/生产可设 warn 只留错误）
        //   server.log_sampling= 抽样分母（1=全量；N=每 N 条 INFO 记 1 条；WARN/ERROR 不抽样）
        {
            const std::string log_level_str = config.get("server.log_level", "info");
            const int log_sampling = config.get_int("server.log_sampling", 1);
            LogLevel log_level = LogLevel::INFO;
            if (log_level_str == "debug") {
                log_level = LogLevel::DEBUG;
            } else if (log_level_str == "warn" || log_level_str == "warning") {
                log_level = LogLevel::WARN;
            } else if (log_level_str == "error") {
                log_level = LogLevel::ERROR;
            } else {
                log_level = LogLevel::INFO;
            }
            Logger::instance().set_level(log_level);
            Logger::instance().set_sampling(log_sampling);
            if (log_level != LogLevel::INFO || log_sampling > 1) {
                LOG_INFO("Log policy: level=" + log_level_str +
                         ", INFO sampling=1/" + std::to_string(log_sampling));
            }
        }
        // ============================================================
        // 4. 初始化数据库连接池（必须先于数据库迁移）
        // ============================================================
        auto& db_manager = DbManager::instance();
        std::string db_path = config.get_db_path();
        // 连接池大小由配置 db.pool_size 控制（默认 4，配置文件设为 16）
        int pool_size = config.get_db_pool_size();

        if (!db_manager.initialize(db_path, pool_size)) {
            LOG_ERROR("Failed to initialize database connection pool");
            return 1;
        }
        LOG_INFO("Database connection pool initialized: " + std::to_string(pool_size) + " connections");

        // ============================================================
        // 5. 初始化数据库（建表迁移）
        // ============================================================
        try {
            init_database();
            LOG_INFO("Database initialized successfully");
        } catch (const std::exception& e) {
            LOG_ERROR("Database initialization failed: " + std::string(e.what()));
            return 1;
        }

        // ============================================================
        // 6. 初始化会话管理器
        // ============================================================
        auto& session_manager = SessionManager::instance();
        int session_ttl = config.get_session_ttl();
        session_manager.initialize(session_ttl);
        LOG_INFO("Session manager initialized: TTL=" + std::to_string(session_ttl) + "s");

        // 启动后台线程，定期批量清理过期会话
        int cleanup_interval = config.get_session_cleanup_interval();
        session_manager.start_cleanup_loop(cleanup_interval);
        LOG_INFO("Session cleanup loop started: interval=" +
                 std::to_string(cleanup_interval) + "s");

        // ============================================================
        // 7. 创建仓储层 (Repository)
        // ============================================================
        auto user_repo = std::make_shared<SqliteUserRepository>(db_manager);
        auto merchant_repo = std::make_shared<SqliteMerchantRepository>(db_manager);
        auto dish_repo = std::make_shared<SqliteDishRepository>(db_manager);
        auto order_repo = std::make_shared<SqliteOrderRepository>(db_manager);

        LOG_INFO("Repositories created");

        // ============================================================
        // 7.5 初始化多级缓存（L1 本地内存 → L2 Redis → L3 数据源）
        // ============================================================
        int local_ttl = config.get_int("cache.local.ttl", constants::DEFAULT_CACHE_TTL_SECONDS);
        size_t local_max_entries = static_cast<size_t>(config.get_int(
            "cache.local.max_entries", constants::DEFAULT_CACHE_LOCAL_MAX_ENTRIES));
        auto local_cache = std::make_shared<infrastructure::cache::LocalCache>(
            local_max_entries, local_ttl);

        std::shared_ptr<infrastructure::cache::RedisClient> redis_client;
        if (config.get_int("cache.redis.enabled", 1) != 0) {
            std::string redis_host = config.get("cache.redis.host", "127.0.0.1");
            int redis_port = config.get_int("cache.redis.port", 6379);
            redis_client = std::make_shared<infrastructure::cache::RedisClient>(
                redis_host, redis_port,
                config.get_int("cache.redis.timeout_ms", 300));
            if (redis_client->ping()) {
                LOG_INFO("Redis cache connected: " + redis_host + ":" +
                         std::to_string(redis_port));
            } else {
                LOG_WARN("Redis cache unavailable (auto-retry with backoff), "
                         "dish cache will fall back to L3 (DB)");
            }
        } else {
            LOG_INFO("Redis cache disabled by config");
        }

        auto multi_level_cache = std::make_shared<infrastructure::cache::MultiLevelCache>(
            local_cache, redis_client);

        int redis_ttl = config.get_int("cache.redis.ttl",
                                       constants::DEFAULT_CACHE_REDIS_TTL_SECONDS);
        LOG_INFO("Multi-level cache ready: local_ttl=" + std::to_string(local_ttl) +
                 "s, redis_ttl=" + std::to_string(redis_ttl) + "s, "
                 "local_max_entries=" + std::to_string(local_max_entries));

        // ============================================================
        // 8. 创建服务层 (Service)
        // ============================================================
        // 业务读路径请求保护器：/api/shops 与 /api/dishes 各自独立（故障隔离）
        auto shops_guard = make_request_guard();
        auto dishes_guard = make_request_guard();

        auto auth_service = std::make_shared<AuthService>(
            user_repo,
            merchant_repo,
            session_manager,
            multi_level_cache,
            local_ttl,
            redis_ttl,
            shops_guard
        );

        auto dish_service = std::make_shared<DishService>(
            dish_repo, multi_level_cache, local_ttl, redis_ttl, dishes_guard);

        auto order_service = std::make_shared<OrderService>(
            order_repo,
            dish_repo,
            user_repo,
            merchant_repo
        );

        LOG_INFO("Services created (request guards: queue=" +
                 std::to_string(dishes_guard->max_queue()) + ", watermark=" +
                 std::to_string(dishes_guard->high_watermark()) + ")");

        // ============================================================
        // 9. 创建处理器 (Handler)
        // ============================================================
        AuthHandler auth_handler(*auth_service, session_manager);
        DishHandler dish_handler(*dish_service, *auth_service);
        OrderHandler order_handler(*order_service, *auth_service, session_manager);

        LOG_INFO("Handlers created");

         // ============================================================
        // 10. 创建路由表 (Router)
        // ============================================================
        HttpRouter router;

        // ---------- 静态页面路由 ----------
        router.get("/", [](const HttpRequest&, HttpResponse& resp) {
            resp.send_file("www/index.html", "text/html; charset=utf-8");
        });

        router.get("/login.html", [](const HttpRequest&, HttpResponse& resp) {
            resp.send_file("www/login.html", "text/html; charset=utf-8");
        });

        router.get("/register.html", [](const HttpRequest&, HttpResponse& resp) {
            resp.send_file("www/register.html", "text/html; charset=utf-8");
        });

        router.get("/merchant_login.html", [](const HttpRequest&, HttpResponse& resp) {
            resp.send_file("www/merchant_login.html", "text/html; charset=utf-8");
        });

        router.get("/merchant_register.html", [](const HttpRequest&, HttpResponse& resp) {
            resp.send_file("www/merchant_register.html", "text/html; charset=utf-8");
        });

        // ---------- 需要认证的后台页面 ----------
        router.get("/customer_dashboard.html",
            [&auth_handler](const HttpRequest& req, HttpResponse& resp) {
                if (!auth_handler.is_user_logged_in(req)) {
                    resp.redirect("/login.html");
                    return;
                }
                resp.send_file("www/customer_dashboard.html", "text/html; charset=utf-8");
            }
        );

        router.get("/merchant_dashboard.html",
            [&auth_handler](const HttpRequest& req, HttpResponse& resp) {
                if (!auth_handler.is_merchant_logged_in(req)) {
                    resp.redirect("/merchant_login.html");
                    return;
                }
                resp.send_file("www/merchant_dashboard.html", "text/html; charset=utf-8");
            }
        );

        // ---------- API路由 - 用户相关 ----------
        router.post("/api/user/register",
            [&auth_handler](const HttpRequest& req, HttpResponse& resp) {
                auth_handler.handle_user_register(req, resp);
            }
        );

        router.post("/api/user/login",
            [&auth_handler](const HttpRequest& req, HttpResponse& resp) {
                auth_handler.handle_user_login(req, resp);
            }
        );

        router.get("/api/user/info",
            [&auth_handler](const HttpRequest& req, HttpResponse& resp) {
                auth_handler.handle_user_info(req, resp);
            }
        );

        router.post("/api/user/logout",
            [&auth_handler](const HttpRequest& req, HttpResponse& resp) {
                auth_handler.handle_user_logout(req, resp);
            }
        );

        // ---------- API路由 - 商家相关 ----------
        router.post("/api/merchant/register",
            [&auth_handler](const HttpRequest& req, HttpResponse& resp) {
                auth_handler.handle_merchant_register(req, resp);
            }
        );

        router.post("/api/merchant/login",
            [&auth_handler](const HttpRequest& req, HttpResponse& resp) {
                auth_handler.handle_merchant_login(req, resp);
            }
        );

        router.get("/api/merchant/info",
            [&auth_handler](const HttpRequest& req, HttpResponse& resp) {
                auth_handler.handle_merchant_info(req, resp);
            }
        );

        router.post("/api/merchant/logout",
            [&auth_handler](const HttpRequest& req, HttpResponse& resp) {
                auth_handler.handle_merchant_logout(req, resp);
            }
        );

        router.put("/api/merchant/status",
            [&auth_handler](const HttpRequest& req, HttpResponse& resp) {
                auth_handler.handle_merchant_status(req, resp);
            }
        );

        router.get("/api/shops",
            [&auth_handler](const HttpRequest& req, HttpResponse& resp) {
                auth_handler.handle_get_shops(req, resp);
            }
        );

        // ---------- API路由 - 菜品相关 ----------
        router.get("/api/dishes",
            [&dish_handler](const HttpRequest& req, HttpResponse& resp) {
                dish_handler.handle_get_dishes(req, resp);
            }
        );

        router.post("/api/dish/add",
            [&dish_handler](const HttpRequest& req, HttpResponse& resp) {
                dish_handler.handle_add_dish(req, resp);
            }
        );

        router.put("/api/dish/edit",
            [&dish_handler](const HttpRequest& req, HttpResponse& resp) {
                dish_handler.handle_edit_dish(req, resp);
            }
        );

        router.put("/api/dish/available",
            [&dish_handler](const HttpRequest& req, HttpResponse& resp) {
                dish_handler.handle_toggle_available(req, resp);
            }
        );

        router.put("/api/dish/delete",
            [&dish_handler](const HttpRequest& req, HttpResponse& resp) {
                dish_handler.handle_delete_dish(req, resp);
            }
        );

        // ---------- API路由 - 订单相关 ----------
        router.post("/api/order/submit",
            [&order_handler](const HttpRequest& req, HttpResponse& resp) {
                order_handler.handle_order_submit(req, resp);
            }
        );

        router.get("/api/orders/my",
            [&order_handler](const HttpRequest& req, HttpResponse& resp) {
                order_handler.handle_my_orders(req, resp);
            }
        );

        router.get("/api/merchant/orders",
            [&order_handler](const HttpRequest& req, HttpResponse& resp) {
                order_handler.handle_merchant_orders(req, resp);
            }
        );

        router.put("/api/order/status",
            [&order_handler](const HttpRequest& req, HttpResponse& resp) {
                order_handler.handle_update_order_status(req, resp);
            }
        );

        // 顾客确认收货（配送中 → 已完成），仅登录用户可调用
        router.post("/api/order/complete",
            [&order_handler](const HttpRequest& req, HttpResponse& resp) {
                order_handler.handle_confirm_delivery(req, resp);
            }
        );

        // ---------- 静态资源路由 ----------
        router.get("*.js", [](const HttpRequest& req, HttpResponse& resp) {
            std::string path = "www" + req.get_path();
            resp.send_file(path, "application/javascript; charset=utf-8");
        });

        router.get("*.css", [](const HttpRequest& req, HttpResponse& resp) {
            std::string path = "www" + req.get_path();
            resp.send_file(path, "text/css; charset=utf-8");
        });

        router.get("*.png", [](const HttpRequest& req, HttpResponse& resp) {
            std::string path = "www" + req.get_path();
            resp.send_file(path, "image/png");
        });

        router.get("*.jpg", [](const HttpRequest& req, HttpResponse& resp) {
            std::string path = "www" + req.get_path();
            resp.send_file(path, "image/jpeg");
        });

        router.get("*.ico", [](const HttpRequest& req, HttpResponse& resp) {
            std::string path = "www" + req.get_path();
            resp.send_file(path, "image/x-icon");
        });

        // ---------- 404处理 ----------
        router.set_not_found_handler([](const HttpRequest&, HttpResponse& resp) {
            resp.set_status(404);
            resp.set_content_type("text/html; charset=utf-8");
            resp.set_body(
                "<!DOCTYPE html>"
                "<html><head><meta charset='utf-8'>"
                "<title>404 Not Found</title></head>"
                "<body><h1>404 Not Found</h1>"
                "<p>The requested URL was not found on this server.</p>"
                "</body></html>"
            );
        });

        LOG_INFO("Routes registered: " + std::to_string(router.count()));

        // ============================================================
        // 11. 创建HTTP服务器
        // ============================================================
        g_server = std::make_unique<HttpServer>(port);
        g_server->set_router(router);
        // 监听地址：接入 Nginx 后配置为 127.0.0.1（后端不直接暴露），默认 0.0.0.0
        g_server->set_bind_address(config.get("server.bind_address", "0.0.0.0"));
        g_server->set_max_connections(config.get_int("server.max_connections", 1024));
        g_server->set_read_timeout(config.get_int("server.read_timeout", 30));
        g_server->set_write_timeout(config.get_int("server.write_timeout", 30));

        // 事件循环线程池大小（0 = 不使用线程池，同步处理）
        int worker_count = config.get_int("server.thread_pool_size",
                                          constants::DEFAULT_THREAD_POOL_SIZE);
        g_server->set_worker_count(worker_count);
        LOG_INFO("Event loop thread pool size: " + std::to_string(worker_count));

        // ---------- 添加中间件 ----------
        // 1. 日志中间件 - 记录所有请求
        g_server->use(std::make_unique<LoggingMiddleware>(false));

        // 2. 限流中间件 - 保护API（参数由 config.json 的 server.rate_limit.* 控制）
        RateLimitMiddleware::Config rate_limit_config;
        rate_limit_config.max_requests =
            config.get_int("server.rate_limit.max_requests", 100);
        rate_limit_config.window_seconds =
            config.get_int("server.rate_limit.window_seconds", 60);
        rate_limit_config.block_seconds =
            config.get_int("server.rate_limit.block_seconds", 300);
        g_server->use(std::make_unique<RateLimitMiddleware>(rate_limit_config));

        // 3. 认证中间件 - 保护需要登录的路径
        auto auth_middleware = std::make_unique<AuthMiddleware>(session_manager);
        auth_middleware->cookie_name("session_id")
            .protect("/api/dish/add")
            .protect("/api/dish/edit")
            .protect("/api/dish/delete")
            .protect("/api/order/submit")
            .protect("/api/order/complete")
            .protect("/api/merchant/status")
            .protect("/customer_dashboard.html")
            .protect("/merchant_dashboard.html")
            .public_path("/api/user/login")
            .public_path("/api/user/register")
            .public_path("/api/merchant/login")
            .public_path("/api/merchant/register")
            .public_path("/api/shops")
            .public_path("/api/dishes");
        g_server->use(std::move(auth_middleware));

        LOG_INFO("Middlewares registered: " + std::to_string(3));

        // ============================================================
        // 12. 设置信号处理
        // ============================================================
        setup_signal_handlers();

        // ============================================================
        // 13. 打印启动信息
        // ============================================================
        print_banner(port);
        LOG_INFO("Listening on " + config.get("server.bind_address", "0.0.0.0") +
                 ":" + std::to_string(port));
        LOG_INFO("✅ Server started successfully");

        // ============================================================
        // 14. 启动服务器（阻塞）
        // ============================================================
        if (!g_server->start()) {
            LOG_ERROR("Failed to start server");
            return 1;
        }

        g_server->event_loop();

        // ============================================================
        // 15. 清理资源
        // ============================================================
        LOG_INFO("Server stopped");
        g_server.reset();
        db_manager.shutdown();
        session_manager.stop_cleanup_loop();
        session_manager.cleanup_expired();
        Logger::instance().shutdown();

        return 0;

    } catch (const InfrastructureException& e) {
        LOG_ERROR("Infrastructure error: " + std::string(e.what()) +
                  " (code=" + std::to_string(e.error_code()) + ")");
        return 1;
    } catch (const std::exception& e) {
        LOG_ERROR("Unexpected error: " + std::string(e.what()));
        return 1;
    } catch (...) {
        LOG_ERROR("Unknown error");
        return 1;
    }
}