// ============================================================
// tests/infrastructure/test_session.cpp
// 对应:
//   - src/infrastructure/session/session_store.{h,cpp}
//   - src/infrastructure/session/session_manager.{h,cpp}
// ============================================================
#include "test_framework.h"

#include <chrono>

#include "infrastructure/session/session_manager.h"
#include "infrastructure/session/session_store.h"

using infrastructure::session::InMemorySessionStore;
using infrastructure::session::SessionInfo;
using infrastructure::session::SessionManager;

namespace {

SessionInfo make_session(const std::string& id, int user_id, int merchant_id,
                         std::chrono::system_clock::time_point expire) {
    SessionInfo info;
    info.session_id = id;
    info.user_id = user_id;
    info.merchant_id = merchant_id;
    info.created_at = std::chrono::system_clock::now();
    info.expire_time = expire;
    return info;
}

}  // namespace

// ---------------- InMemorySessionStore ----------------

TEST(InMemorySessionStore, SaveAndFind) {
    InMemorySessionStore store;
    auto now = std::chrono::system_clock::now();
    SessionInfo info = make_session("abc123", 7, 3, now + std::chrono::hours(1));

    EXPECT_TRUE(store.save(info));
    auto found = store.find("abc123");
    EXPECT_TRUE(found.has_value());
    EXPECT_STREQ(found->session_id, "abc123");
    EXPECT_EQ(found->user_id, 7);
    EXPECT_EQ(found->merchant_id, 3);
}

TEST(InMemorySessionStore, FindMissingReturnsNullopt) {
    InMemorySessionStore store;
    EXPECT_FALSE(store.find("missing").has_value());
}

TEST(InMemorySessionStore, ExpiredSessionIsRemoved) {
    InMemorySessionStore store;
    auto past = std::chrono::system_clock::now() - std::chrono::seconds(10);
    SessionInfo expired = make_session("expired", 1, 1, past);

    store.save(expired);
    // 过期会话在 find 时被自动清除
    EXPECT_FALSE(store.find("expired").has_value());
    // 再次查找也不存在
    EXPECT_FALSE(store.find("expired").has_value());
}

TEST(InMemorySessionStore, NonExpiredSessionSurvives) {
    InMemorySessionStore store;
    auto future = std::chrono::system_clock::now() + std::chrono::hours(1);
    SessionInfo live = make_session("live", 1, 1, future);
    store.save(live);
    EXPECT_TRUE(store.find("live").has_value());
}

TEST(InMemorySessionStore, RemoveReturnsSuccess) {
    InMemorySessionStore store;
    auto future = std::chrono::system_clock::now() + std::chrono::hours(1);
    store.save(make_session("x", 1, 1, future));

    EXPECT_TRUE(store.remove("x"));
    EXPECT_FALSE(store.find("x").has_value());
    // 再次删除已不存在的会话返回 false
    EXPECT_FALSE(store.remove("x"));
}

TEST(InMemorySessionStore, OverwriteSameId) {
    InMemorySessionStore store;
    auto future = std::chrono::system_clock::now() + std::chrono::hours(1);
    store.save(make_session("dup", 1, 1, future));
    store.save(make_session("dup", 2, 2, future));

    auto found = store.find("dup");
    EXPECT_TRUE(found.has_value());
    EXPECT_EQ(found->user_id, 2);
}

// ---------------- SessionManager（单例） ----------------

TEST(SessionManager, CreateValidateDestroy) {
    auto& sm = SessionManager::instance();
    sm.initialize(3600);

    std::string sid = sm.create_session(1, 2);
    EXPECT_EQ(sid.size(), static_cast<size_t>(32));
    EXPECT_TRUE(sm.validate_session(sid));

    sm.destroy_session(sid);
    EXPECT_FALSE(sm.validate_session(sid));
}

TEST(SessionManager, ValidateUnknownReturnsFalse) {
    auto& sm = SessionManager::instance();
    EXPECT_FALSE(sm.validate_session("definitely_not_a_session"));
}

TEST(SessionManager, DestroyUnknownIsHarmless) {
    auto& sm = SessionManager::instance();
    // 不应抛异常
    EXPECT_NO_THROW(sm.destroy_session("unknown_id"));
}

TEST(SessionManager, GenerateHexIdProducesRequestedLength) {
    EXPECT_EQ(SessionManager::generate_hex_id(8).size(), static_cast<size_t>(8));
    EXPECT_EQ(SessionManager::generate_hex_id(32).size(), static_cast<size_t>(32));
    EXPECT_EQ(SessionManager::generate_hex_id(0).size(), static_cast<size_t>(0));
}

TEST(SessionManager, GenerateHexIdContainsOnlyHexChars) {
    const std::string id = SessionManager::generate_hex_id(64);
    for (char c : id) {
        const bool is_hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        if (!is_hex) {
            FAIL();
        }
    }
    SUCCEED();
}
