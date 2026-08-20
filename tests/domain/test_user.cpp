// ============================================================
// tests/domain/test_user.cpp
// 对应: src/domain/models/user.{h,cpp}
// ============================================================
#include "test_framework.h"

#include "domain/models/user.h"

TEST(User, ConstructorAndGetters) {
    User u(1, "alice", "hash123", true);
    EXPECT_EQ(u.get_id(), 1);
    EXPECT_STREQ(u.get_username(), "alice");
    EXPECT_STREQ(u.get_password_hash(), "hash123");
    EXPECT_TRUE(u.is_active());
}

TEST(User, DefaultConstruct) {
    User u;
    EXPECT_EQ(u.get_id(), 0);
    EXPECT_STREQ(u.get_username(), "");
    EXPECT_STREQ(u.get_password_hash(), "");
    EXPECT_TRUE(u.is_active());
}

TEST(User, DefaultActiveWhenNotSpecified) {
    User u(2, "bob", "h");
    EXPECT_TRUE(u.is_active());
}

TEST(User, SetActive) {
    User u(3, "carol", "h", true);
    u.set_active(false);
    EXPECT_FALSE(u.is_active());
    u.set_active(true);
    EXPECT_TRUE(u.is_active());
}

TEST(User, InactiveOnConstruct) {
    User u(4, "dave", "h", false);
    EXPECT_FALSE(u.is_active());
}
