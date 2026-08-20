// ============================================================
// tests/infrastructure/test_config.cpp
// 对应: src/infrastructure/common/config.{h,cpp}
// 注意：
//   - Config 是单例，无法真正清空状态。
//   - use_defaults() 只填充「缺失」的键；因此测试
//     UseDefaultsFillsMissing 必须排在最先（此时单例为空，
//     所有键都缺失），否则会被其它测试写入的状态污染。
// ============================================================
#include "test_framework.h"

#include <cstdio>
#include <fstream>

#include "infrastructure/common/config.h"
#include "infrastructure/common/exception.h"

namespace {

const char* kConfigPath = "test_config_tmp.ini";

void reset_keys() {
    auto& cfg = infrastructure::common::Config::instance();
    cfg.set("db.path", "");
    cfg.set("db.pool_size", "");
    cfg.set("session.ttl", "");
}

}  // namespace

// 必须最先运行：此时单例尚未被任何测试写入，db.* / session.ttl 均缺失
TEST(Config, UseDefaultsFillsMissing) {
    auto& cfg = infrastructure::common::Config::instance();
    cfg.use_defaults();
    EXPECT_STREQ(cfg.get_db_path(), "food_delivery.db");
    EXPECT_EQ(cfg.get_db_pool_size(), 4);
    EXPECT_EQ(cfg.get_session_ttl(), 3600);
}

TEST(Config, UseDefaultsKeepsExistingValues) {
    reset_keys();
    auto& cfg = infrastructure::common::Config::instance();
    cfg.set("db.path", "custom.db");
    cfg.use_defaults();
    EXPECT_STREQ(cfg.get_db_path(), "custom.db");
    EXPECT_EQ(cfg.get_db_pool_size(), 4);
}

TEST(Config, LoadParsesKeyValuePairs) {
    {
        std::ofstream f(kConfigPath);
        f << "# 注释行应被跳过\n"
          << "\n"
          << "db.path = test.db\n"
          << "db.pool_size = 8\n"
          << "session.ttl = 7200\n"
          << "unknown.key = value\n";
    }

    reset_keys();
    auto& cfg = infrastructure::common::Config::instance();
    EXPECT_TRUE(cfg.load(kConfigPath));
    EXPECT_STREQ(cfg.get_db_path(), "test.db");
    EXPECT_EQ(cfg.get_db_pool_size(), 8);
    EXPECT_EQ(cfg.get_session_ttl(), 7200);
    EXPECT_STREQ(cfg.get("unknown.key", ""), "value");

    std::remove(kConfigPath);
}

TEST(Config, LoadMissingFileThrows) {
    auto& cfg = infrastructure::common::Config::instance();
    EXPECT_THROW(cfg.load("nonexistent_config_file_xyz.ini"), infrastructure::common::ConfigLoadException);
}

TEST(Config, GetIntInvalidReturnsDefault) {
    auto& cfg = infrastructure::common::Config::instance();
    cfg.set("some.invalid.int", "not-a-number");
    EXPECT_EQ(cfg.get_int("some.invalid.int", 42), 42);
}

TEST(Config, GetIntMissingReturnsDefault) {
    auto& cfg = infrastructure::common::Config::instance();
    EXPECT_EQ(cfg.get_int("missing.key", 7), 7);
}

TEST(Config, GetMissingReturnsDefault) {
    auto& cfg = infrastructure::common::Config::instance();
    EXPECT_STREQ(cfg.get("missing.key", "fallback"), "fallback");
}

TEST(Config, SetThenGet) {
    auto& cfg = infrastructure::common::Config::instance();
    cfg.set("custom.key", "custom-value");
    EXPECT_STREQ(cfg.get("custom.key", ""), "custom-value");
}
