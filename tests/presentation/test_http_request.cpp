// ============================================================
// tests/presentation/test_http_request.cpp
// 对应: src/presentation/http/http_request.{h,cpp}
// ============================================================
#include "test_framework.h"

#include "presentation/http/http_request.h"

using presentation::http::HttpRequest;

TEST(HttpRequest, GetPathReturnsPath) {
    HttpRequest req;
    req.path = "/api/user/info";
    EXPECT_STREQ(req.get_path(), "/api/user/info");
}

TEST(HttpRequest, HeaderExactMatch) {
    HttpRequest req;
    req.headers["Content-Type"] = "application/json";
    EXPECT_STREQ(req.header("Content-Type"), "application/json");
}

TEST(HttpRequest, HeaderCaseInsensitive) {
    HttpRequest req;
    req.headers["Content-Type"] = "application/json";
    EXPECT_STREQ(req.header("content-type"), "application/json");
    EXPECT_STREQ(req.header("CONTENT-TYPE"), "application/json");
    EXPECT_STREQ(req.header("cOnTeNt-TyPe"), "application/json");
}

TEST(HttpRequest, HeaderMissingReturnsEmpty) {
    HttpRequest req;
    EXPECT_STREQ(req.header("X-Missing"), "");
}

TEST(HttpRequest, QueryReturnsValue) {
    HttpRequest req;
    req.query_params["id"] = "42";
    EXPECT_STREQ(req.query("id"), "42");
}

TEST(HttpRequest, QueryMissingReturnsEmpty) {
    HttpRequest req;
    EXPECT_STREQ(req.query("missing"), "");
}
