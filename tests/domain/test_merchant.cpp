// ============================================================
// tests/domain/test_merchant.cpp
// 对应: src/domain/models/merchant.{h,cpp}
// ============================================================
#include "test_framework.h"

#include "domain/models/merchant.h"

TEST(Merchant, ConstructorAndGetters) {
    Merchant m(1, "shop_owner", "老王餐馆", "深圳市南山区", true);
    EXPECT_EQ(m.get_id(), 1);
    EXPECT_STREQ(m.get_username(), "shop_owner");
    EXPECT_STREQ(m.get_shop_name(), "老王餐馆");
    EXPECT_STREQ(m.get_address(), "深圳市南山区");
    EXPECT_TRUE(m.is_open());
}

TEST(Merchant, DefaultConstruct) {
    Merchant m;
    EXPECT_EQ(m.get_id(), 0);
    EXPECT_STREQ(m.get_username(), "");
    EXPECT_STREQ(m.get_shop_name(), "");
    EXPECT_STREQ(m.get_address(), "");
    EXPECT_TRUE(m.is_open());
}

TEST(Merchant, DefaultOpenWhenNotSpecified) {
    Merchant m(2, "u", "店", "地址");
    EXPECT_TRUE(m.is_open());
}

TEST(Merchant, SetOpen) {
    Merchant m(3, "u", "店", "地址", true);
    m.set_open(false);
    EXPECT_FALSE(m.is_open());
    m.set_open(true);
    EXPECT_TRUE(m.is_open());
}

TEST(Merchant, ClosedOnConstruct) {
    Merchant m(4, "u", "店", "地址", false);
    EXPECT_FALSE(m.is_open());
}
