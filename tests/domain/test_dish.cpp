// ============================================================
// tests/domain/test_dish.cpp
// 对应: src/domain/models/dish.{h,cpp}
// 注意：has_active_orders() 目前是占位实现（恒返回 false）
// ============================================================
#include "test_framework.h"

#include "domain/models/dish.h"

TEST(Dish, ConstructorAndGetters) {
    Dish d(5, "红烧肉", Money(38.0), "热菜", "肥而不腻");
    EXPECT_EQ(d.get_merchant_id(), 5);
    EXPECT_STREQ(d.get_name(), "红烧肉");
    EXPECT_NEAR(d.get_price().get_yuan(), 38.0, 1e-9);
    EXPECT_STREQ(d.get_category(), "热菜");
    EXPECT_STREQ(d.get_description(), "肥而不腻");
    EXPECT_TRUE(d.is_available());
    EXPECT_FALSE(d.is_deleted());
}

TEST(Dish, DefaultConstruct) {
    Dish d;
    EXPECT_EQ(d.get_merchant_id(), 0);
    EXPECT_STREQ(d.get_name(), "");
    EXPECT_NEAR(d.get_price().get_yuan(), 0.0, 1e-9);
    EXPECT_STREQ(d.get_category(), "");
    EXPECT_STREQ(d.get_description(), "");
    EXPECT_TRUE(d.is_available());
    EXPECT_FALSE(d.is_deleted());
}

TEST(Dish, SetAvailable) {
    Dish d(1, "菜", Money(10.0), "菜", "");
    d.set_available(false);
    EXPECT_FALSE(d.is_available());
    d.set_available(true);
    EXPECT_TRUE(d.is_available());
}

TEST(Dish, SoftDeleteMarksDeleted) {
    Dish d(1, "菜", Money(10.0), "菜", "");
    d.soft_delete();
    EXPECT_TRUE(d.is_deleted());
}

TEST(Dish, DeletableWhenNoActiveOrders) {
    // has_active_orders() 目前恒为 false，因此初始可删除
    Dish d(1, "菜", Money(10.0), "菜", "");
    EXPECT_TRUE(d.is_deletable());
}

TEST(Dish, NotDeletableAfterSoftDelete) {
    Dish d(1, "菜", Money(10.0), "菜", "");
    d.soft_delete();
    EXPECT_FALSE(d.is_deletable());
}

TEST(Dish, DeletableEvenWhenUnavailable) {
    Dish d(1, "菜", Money(10.0), "菜", "");
    d.set_available(false);
    EXPECT_TRUE(d.is_deletable());
}
