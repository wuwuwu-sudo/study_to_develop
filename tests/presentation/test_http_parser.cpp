// ============================================================
// tests/presentation/test_http_parser.cpp
// 对应: src/presentation/http/http_parser.{h,cpp}
// ============================================================
#include "test_framework.h"

#include "presentation/http/http_parser.h"

using presentation::http::HttpParser;
using presentation::http::HttpRequest;

TEST(HttpParser, ParseGetRequestLine) {
    HttpParser parser;
    HttpRequest req;
    const std::string raw =
        "GET /api/user/info HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "\r\n";

    EXPECT_TRUE(parser.parse(raw, req));
    EXPECT_EQ(static_cast<int>(req.method), static_cast<int>(HttpMethod::GET));
    EXPECT_STREQ(req.path, "/api/user/info");
    EXPECT_STREQ(req.version, "HTTP/1.1");
}

TEST(HttpParser, ParseGetWithQuery) {
    HttpParser parser;
    HttpRequest req;
    const std::string raw =
        "GET /api/dishes?merchant_id=5&page=2 HTTP/1.1\r\n"
        "\r\n";

    EXPECT_TRUE(parser.parse(raw, req));
    EXPECT_STREQ(req.path, "/api/dishes");
    EXPECT_STREQ(req.query("merchant_id"), "5");
    EXPECT_STREQ(req.query("page"), "2");
    EXPECT_STREQ(req.query("missing"), "");
}

TEST(HttpParser, ParsePostHeadersAndBody) {
    HttpParser parser;
    HttpRequest req;
    const std::string raw =
        "POST /api/user/login HTTP/1.1\r\n"
        "Content-Type: application/json\r\n"
        "Cookie: session_id=abc123\r\n"
        "\r\n"
        R"({"username":"alice","password":"x"})";

    EXPECT_TRUE(parser.parse(raw, req));
    EXPECT_EQ(static_cast<int>(req.method), static_cast<int>(HttpMethod::POST));
    EXPECT_STREQ(req.path, "/api/user/login");
    EXPECT_STREQ(req.header("Content-Type"), "application/json");
    EXPECT_STREQ(req.header("Cookie"), "session_id=abc123");
    EXPECT_STREQ(req.body, R"({"username":"alice","password":"x"})");
}

TEST(HttpParser, ParsePutMethod) {
    HttpParser parser;
    HttpRequest req;
    const std::string raw =
        "PUT /api/dish/available HTTP/1.1\r\n"
        "\r\n";
    EXPECT_TRUE(parser.parse(raw, req));
    EXPECT_EQ(static_cast<int>(req.method), static_cast<int>(HttpMethod::PUT));
}

TEST(HttpParser, ParseDeleteMethod) {
    HttpParser parser;
    HttpRequest req;
    const std::string raw =
        "DELETE /api/dish/delete HTTP/1.1\r\n"
        "\r\n";
    EXPECT_TRUE(parser.parse(raw, req));
    EXPECT_EQ(static_cast<int>(req.method), static_cast<int>(HttpMethod::DELETE));
}

TEST(HttpParser, RejectUnsupportedMethod) {
    HttpParser parser;
    HttpRequest req;
    const std::string raw =
        "HEAD / HTTP/1.1\r\n"
        "\r\n";
    EXPECT_FALSE(parser.parse(raw, req));
}

TEST(HttpParser, RejectEmptyInput) {
    HttpParser parser;
    HttpRequest req;
    EXPECT_FALSE(parser.parse("", req));
}

TEST(HttpParser, RejectGarbageMethod) {
    HttpParser parser;
    HttpRequest req;
    const std::string raw = "FOO / HTTP/1.1\r\n\r\n";
    EXPECT_FALSE(parser.parse(raw, req));
}

TEST(HttpParser, PathWithoutQuery) {
    HttpParser parser;
    HttpRequest req;
    const std::string raw = "GET / HTTP/1.1\r\n\r\n";
    EXPECT_TRUE(parser.parse(raw, req));
    EXPECT_STREQ(req.path, "/");
}
