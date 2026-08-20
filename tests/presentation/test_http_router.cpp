// ============================================================
// tests/presentation/test_http_router.cpp
// 对应: src/presentation/http/http_router.{h,cpp}
// ============================================================
#include "test_framework.h"

#include "presentation/http/http_router.h"

using presentation::http::HttpRequest;
using presentation::http::HttpResponse;
using presentation::http::HttpRouter;

TEST(HttpRouter, RegisterRoutesIncreasesCount) {
    HttpRouter router;
    auto noop = [](const HttpRequest&, HttpResponse&) {};
    router.get("/a", noop);
    router.post("/b", noop);
    router.put("/c", noop);
    router.delete_route("/d", noop);
    EXPECT_EQ(router.count(), static_cast<size_t>(4));
}

TEST(HttpRouter, EmptyRouterHasZeroCount) {
    HttpRouter router;
    EXPECT_EQ(router.count(), static_cast<size_t>(0));
}

TEST(HttpRouter, DispatchExactMatch) {
    HttpRouter router;
    router.get("/hello", [](const HttpRequest&, HttpResponse& resp) {
        resp.set_status(200);
        resp.set_body("hi");
    });

    HttpRequest req;
    req.method = HttpMethod::GET;
    req.path = "/hello";
    HttpResponse resp;

    EXPECT_TRUE(router.dispatch(req, resp));
    EXPECT_EQ(resp.status_code, 200);
    EXPECT_STREQ(resp.body, "hi");
}

TEST(HttpRouter, DispatchMethodMismatchFallsThrough) {
    HttpRouter router;
    router.get("/hello", [](const HttpRequest&, HttpResponse& resp) {
        resp.set_body("hi");
    });

    HttpRequest req;
    req.method = HttpMethod::POST;  // 注册的是 GET
    req.path = "/hello";
    HttpResponse resp;

    // 无 not_found handler 时返回 false，并置 404
    EXPECT_FALSE(router.dispatch(req, resp));
    EXPECT_EQ(resp.status_code, 404);
    EXPECT_STREQ(resp.body, "Not Found");
}

TEST(HttpRouter, DispatchWildcardSuffixMatch) {
    HttpRouter router;
    router.get("*.js", [](const HttpRequest&, HttpResponse& resp) {
        resp.set_status(200);
        resp.set_body("js");
    });

    HttpRequest req;
    req.method = HttpMethod::GET;
    req.path = "/app.js";
    HttpResponse resp;

    EXPECT_TRUE(router.dispatch(req, resp));
    EXPECT_EQ(resp.status_code, 200);
    EXPECT_STREQ(resp.body, "js");
}

TEST(HttpRouter, DispatchWildcardNoMatch) {
    HttpRouter router;
    router.get("*.js", [](const HttpRequest&, HttpResponse& resp) {
        resp.set_body("js");
    });

    HttpRequest req;
    req.method = HttpMethod::GET;
    req.path = "/style.css";
    HttpResponse resp;

    EXPECT_FALSE(router.dispatch(req, resp));
    EXPECT_EQ(resp.status_code, 404);
}

TEST(HttpRouter, NotFoundHandlerIsInvoked) {
    HttpRouter router;
    router.get("/known", [](const HttpRequest&, HttpResponse& resp) {
        resp.set_body("ok");
    });
    router.set_not_found_handler([](const HttpRequest&, HttpResponse& resp) {
        resp.set_status(404);
        resp.set_body("custom 404");
    });

    HttpRequest req;
    req.method = HttpMethod::GET;
    req.path = "/unknown";
    HttpResponse resp;

    // 有 not_found handler 时 dispatch 返回 true（已处理）
    EXPECT_TRUE(router.dispatch(req, resp));
    EXPECT_EQ(resp.status_code, 404);
    EXPECT_STREQ(resp.body, "custom 404");
}

TEST(HttpRouter, FirstMatchingRouteWins) {
    HttpRouter router;
    router.get("/dup", [](const HttpRequest&, HttpResponse& resp) { resp.set_body("first"); });
    router.get("/dup", [](const HttpRequest&, HttpResponse& resp) { resp.set_body("second"); });

    HttpRequest req;
    req.method = HttpMethod::GET;
    req.path = "/dup";
    HttpResponse resp;

    EXPECT_TRUE(router.dispatch(req, resp));
    EXPECT_STREQ(resp.body, "first");
}

TEST(HttpRouter, DispatchRespectsMethodForWildcard) {
    HttpRouter router;
    router.get("*.css", [](const HttpRequest&, HttpResponse& resp) { resp.set_body("css"); });

    HttpRequest req;
    req.method = HttpMethod::POST;  // 匹配路径但方法不符
    req.path = "/style.css";
    HttpResponse resp;

    EXPECT_FALSE(router.dispatch(req, resp));
}
