// ============================================================
// tests/presentation/test_http_response.cpp
// 对应: src/presentation/http/http_response.{h,cpp}
// ============================================================
#include "test_framework.h"

#include <cstdio>
#include <fstream>

#include "presentation/http/http_response.h"

using presentation::http::HttpResponse;

namespace {
const char* kTestFile = "test_send_file_tmp.txt";
}

TEST(HttpResponse, DefaultState) {
    HttpResponse resp;
    EXPECT_EQ(resp.status_code, 200);
    EXPECT_STREQ(resp.body, "");
}

TEST(HttpResponse, SetStatus) {
    HttpResponse resp;
    resp.set_status(404);
    EXPECT_EQ(resp.status_code, 404);
}

TEST(HttpResponse, SetJson) {
    HttpResponse resp;
    resp.set_json(R"({"ok":true})");
    EXPECT_STREQ(resp.headers["Content-Type"], "application/json");
    EXPECT_STREQ(resp.body, R"({"ok":true})");
}

TEST(HttpResponse, SetText) {
    HttpResponse resp;
    resp.set_text("hello");
    EXPECT_STREQ(resp.headers["Content-Type"], "text/plain");
    EXPECT_STREQ(resp.body, "hello");
}

TEST(HttpResponse, SetBodyOnly) {
    HttpResponse resp;
    resp.set_body("raw");
    EXPECT_STREQ(resp.body, "raw");
    // 不改变 Content-Type
    EXPECT_EQ(resp.headers.count("Content-Type"), static_cast<size_t>(0));
}

TEST(HttpResponse, SetHeader) {
    HttpResponse resp;
    resp.set_header("X-Custom", "abc");
    EXPECT_STREQ(resp.headers["X-Custom"], "abc");
}

TEST(HttpResponse, Redirect) {
    HttpResponse resp;
    resp.redirect("/login.html");
    EXPECT_EQ(resp.status_code, 302);
    EXPECT_STREQ(resp.headers["Location"], "/login.html");
}

TEST(HttpResponse, SerializeContainsStatusLine) {
    HttpResponse resp;
    resp.set_status(200);
    std::string s = resp.serialize();
    EXPECT_TRUE(s.find("HTTP/1.1 200 OK\r\n") == 0);
}

TEST(HttpResponse, SerializeIncludesContentLength) {
    HttpResponse resp;
    resp.set_body("abc");
    std::string s = resp.serialize();
    EXPECT_TRUE(s.find("Content-Length: 3\r\n") != std::string::npos);
}

TEST(HttpResponse, SerializeEndsWithBody) {
    HttpResponse resp;
    resp.set_status(404);
    resp.set_body("Not Found");
    std::string s = resp.serialize();
    // 头部与正文以空行分隔，正文在末尾
    EXPECT_TRUE(s.find("\r\n\r\nNot Found") != std::string::npos);
    EXPECT_EQ(s.substr(s.size() - 9), "Not Found");
}

TEST(HttpResponse, SerializeIncludesHeaders) {
    HttpResponse resp;
    resp.set_header("X-Test", "1");
    std::string s = resp.serialize();
    EXPECT_TRUE(s.find("X-Test: 1\r\n") != std::string::npos);
}

TEST(HttpResponse, SendFileMissingReturns404) {
    HttpResponse resp;
    resp.send_file("no_such_file_exists.xyz", "text/plain");
    EXPECT_EQ(resp.status_code, 404);
    EXPECT_STREQ(resp.body, "Not Found");
}

TEST(HttpResponse, SendFileExistingReturnsContent) {
    {
        std::ofstream f(kTestFile);
        f << "file-content";
    }
    HttpResponse resp;
    resp.send_file(kTestFile, "text/plain");
    EXPECT_EQ(resp.status_code, 200);
    EXPECT_STREQ(resp.body, "file-content");
    EXPECT_STREQ(resp.headers["Content-Type"], "text/plain");
    std::remove(kTestFile);
}
