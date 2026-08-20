// ============================================================
// tests/middleware/test_middleware.cpp
// 对应:
//   - src/middleware/middleware.{h,cpp}
//   - src/middleware/auth_middleware.{h,cpp}
//   - src/middleware/rate_limit_middleware.{h,cpp}
//   - src/middleware/logging_middleware.{h,cpp}
// ============================================================
#include "test_framework.h"

#include <iostream>
#include <memory>
#include <sstream>
#include <vector>

#include "infrastructure/session/session_manager.h"
#include "middleware/auth_middleware.h"
#include "middleware/logging_middleware.h"
#include "middleware/middleware.h"
#include "middleware/rate_limit_middleware.h"

using infrastructure::session::SessionManager;
using presentation::http::HttpRequest;
using presentation::http::HttpResponse;
using presentation::middleware::AuthMiddleware;
using presentation::middleware::LoggingMiddleware;
using presentation::middleware::Middleware;
using presentation::middleware::MiddlewarePipeline;
using presentation::middleware::RateLimitMiddleware;

// ---------------- MiddlewarePipeline ----------------

namespace {

// 记录执行顺序的测试中间件
class RecordingMiddleware : public Middleware {
public:
    RecordingMiddleware(int tag, std::vector<int>& order)
        : tag_(tag), order_(order) {}

    void handle(const HttpRequest&, HttpResponse&, Next next) override {
        order_.push_back(tag_);
        next();
    }

private:
    int tag_;
    std::vector<int>& order_;
};

}  // namespace

TEST(MiddlewarePipeline, ExecutesInRegistrationOrder) {
    MiddlewarePipeline pipeline;
    std::vector<int> order;

    pipeline.use(std::make_shared<RecordingMiddleware>(1, order));
    pipeline.use(std::make_shared<RecordingMiddleware>(2, order));
    pipeline.use(std::make_shared<RecordingMiddleware>(3, order));

    HttpRequest req;
    HttpResponse resp;
    pipeline.execute(req, resp);

    EXPECT_EQ(order.size(), static_cast<size_t>(3));
    EXPECT_EQ(order[0], 1);
    EXPECT_EQ(order[1], 2);
    EXPECT_EQ(order[2], 3);
}

TEST(MiddlewarePipeline, EmptyPipelineIsSafe) {
    MiddlewarePipeline pipeline;
    HttpRequest req;
    HttpResponse resp;
    EXPECT_NO_THROW(pipeline.execute(req, resp));
}

TEST(MiddlewarePipeline, NullMiddlewareIsIgnored) {
    MiddlewarePipeline pipeline;
    std::vector<int> order;
    pipeline.use(nullptr);
    pipeline.use(std::make_shared<RecordingMiddleware>(9, order));

    HttpRequest req;
    HttpResponse resp;
    pipeline.execute(req, resp);
    EXPECT_EQ(order.size(), static_cast<size_t>(1));
    EXPECT_EQ(order[0], 9);
}

// ---------------- AuthMiddleware ----------------

TEST(AuthMiddleware, PublicPathBypassesAuth) {
    auto& sm = SessionManager::instance();
    AuthMiddleware mw(sm);
    mw.public_path("/api/shops").protect("/api/dish/add");

    HttpRequest req;
    req.path = "/api/shops";
    HttpResponse resp;
    bool next_called = false;
    Middleware::Next next = [&]() { next_called = true; };

    mw.handle(req, resp, next);
    EXPECT_TRUE(next_called);
}

TEST(AuthMiddleware, ProtectedPathWithoutSessionReturns401) {
    auto& sm = SessionManager::instance();
    AuthMiddleware mw(sm);
    mw.protect("/api/dish/add");

    HttpRequest req;
    req.path = "/api/dish/add";
    HttpResponse resp;
    bool next_called = false;
    Middleware::Next next = [&]() { next_called = true; };

    mw.handle(req, resp, next);
    EXPECT_FALSE(next_called);
    EXPECT_EQ(resp.status_code, 401);
    EXPECT_STREQ(resp.body, "Unauthorized");
}

TEST(AuthMiddleware, ProtectedPathWithValidSessionPasses) {
    auto& sm = SessionManager::instance();
    std::string sid = sm.create_session(1, 0);

    AuthMiddleware mw(sm);
    mw.protect("/api/dish/add");

    HttpRequest req;
    req.path = "/api/dish/add";
    req.headers["session_id"] = sid;
    HttpResponse resp;
    bool next_called = false;
    Middleware::Next next = [&]() { next_called = true; };

    mw.handle(req, resp, next);
    EXPECT_TRUE(next_called);

    sm.destroy_session(sid);
}

TEST(AuthMiddleware, UnlistedPathPassesThrough) {
    auto& sm = SessionManager::instance();
    AuthMiddleware mw(sm);
    mw.protect("/api/dish/add");

    HttpRequest req;
    req.path = "/public/unlisted";
    HttpResponse resp;
    bool next_called = false;
    Middleware::Next next = [&]() { next_called = true; };

    mw.handle(req, resp, next);
    EXPECT_TRUE(next_called);
}

TEST(AuthMiddleware, CustomCookieName) {
    auto& sm = SessionManager::instance();
    std::string sid = sm.create_session(1, 0);

    AuthMiddleware mw(sm);
    mw.cookie_name("my_token").protect("/admin");

    HttpRequest req;
    req.path = "/admin";
    req.headers["my_token"] = sid;
    HttpResponse resp;
    bool next_called = false;
    Middleware::Next next = [&]() { next_called = true; };

    mw.handle(req, resp, next);
    EXPECT_TRUE(next_called);

    sm.destroy_session(sid);
}

// ---------------- RateLimitMiddleware ----------------

TEST(RateLimitMiddleware, AllowsUpToLimitThenBlocks) {
    RateLimitMiddleware::Config cfg;
    cfg.max_requests = 2;
    RateLimitMiddleware mw(cfg);

    HttpRequest req;
    req.headers["X-Real-IP"] = "1.2.3.4";
    HttpResponse resp;

    bool next_called = false;
    Middleware::Next next = [&]() { next_called = true; };

    mw.handle(req, resp, next);
    EXPECT_TRUE(next_called);

    next_called = false;
    mw.handle(req, resp, next);
    EXPECT_TRUE(next_called);

    // 第三次应被限流
    next_called = false;
    mw.handle(req, resp, next);
    EXPECT_FALSE(next_called);
    EXPECT_EQ(resp.status_code, 429);
    EXPECT_STREQ(resp.body, "Too Many Requests");
}

TEST(RateLimitMiddleware, DefaultConfigAllowsOneHundred) {
    RateLimitMiddleware mw;  // 默认 max_requests = 100

    HttpRequest req;
    req.headers["X-Real-IP"] = "10.0.0.1";
    HttpResponse resp;

    bool next_called = false;
    Middleware::Next next = [&]() { next_called = true; };

    // 前 100 次放行
    for (int i = 0; i < 100; ++i) {
        next_called = false;
        mw.handle(req, resp, next);
        EXPECT_TRUE(next_called);
    }

    // 第 101 次被拒
    next_called = false;
    mw.handle(req, resp, next);
    EXPECT_FALSE(next_called);
    EXPECT_EQ(resp.status_code, 429);
}

// ---------------- LoggingMiddleware ----------------

TEST(LoggingMiddleware, CallsNextAndLogs) {
    LoggingMiddleware mw(false);

    std::ostringstream captured;
    std::streambuf* old_buf = std::cout.rdbuf(captured.rdbuf());

    HttpRequest req;
    req.path = "/api/x";
    HttpResponse resp;
    resp.status_code = 201;

    bool next_called = false;
    Middleware::Next next = [&]() { next_called = true; };
    mw.handle(req, resp, next);

    std::cout.rdbuf(old_buf);

    EXPECT_TRUE(next_called);
    EXPECT_TRUE(captured.str().find("/api/x") != std::string::npos);
    EXPECT_TRUE(captured.str().find("201") != std::string::npos);
}
