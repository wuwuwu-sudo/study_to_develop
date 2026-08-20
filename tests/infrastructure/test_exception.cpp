// ============================================================
// tests/infrastructure/test_exception.cpp
// 对应: src/infrastructure/common/exception.{h,cpp}
// ============================================================
#include "test_framework.h"

#include <stdexcept>

#include "infrastructure/common/exception.h"

using infrastructure::common::AppException;
using infrastructure::common::ConfigLoadException;
using infrastructure::common::InfrastructureException;

TEST(Exception, AppExceptionIsRuntimeError) {
    AppException e("boom");
    EXPECT_STREQ(e.what(), "boom");
    // 应可通过 std::runtime_error 捕获
    bool caught = false;
    try {
        throw e;
    } catch (const std::runtime_error&) {
        caught = true;
    }
    EXPECT_TRUE(caught);
}

TEST(Exception, InfrastructureExceptionWithCode) {
    InfrastructureException e("io error", 42);
    EXPECT_STREQ(e.what(), "io error");
    EXPECT_EQ(e.error_code(), 42);
}

TEST(Exception, InfrastructureExceptionDefaultCode) {
    InfrastructureException e("generic");
    EXPECT_EQ(e.error_code(), 0);
}

TEST(Exception, ConfigLoadExceptionCodeIsOne) {
    ConfigLoadException e("bad config");
    EXPECT_STREQ(e.what(), "bad config");
    EXPECT_EQ(e.error_code(), 1);
}

TEST(Exception, ConfigLoadExceptionIsInfrastructureException) {
    // 向上转型应成立
    bool caught = false;
    try {
        throw ConfigLoadException("x");
    } catch (const InfrastructureException& ex) {
        caught = true;
        EXPECT_EQ(ex.error_code(), 1);
    }
    EXPECT_TRUE(caught);
}
