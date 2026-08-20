// ============================================================
// tests/domain/test_order_item.cpp
// 对应: src/domain/models/order_item.{h,cpp}
// ============================================================
#include "test_framework.h"

#include "domain/models/order_item.h"

TEST(OrderItem, DefaultConstruct) {
    OrderItem item;
    EXPECT_EQ(item.get_dish_id(), 0);
    EXPECT_STREQ(item.get_dish_name(), "");
    EXPECT_NEAR(item.get_price().get_yuan(), 0.0, 1e-9);
    EXPECT_EQ(item.get_quantity(), 0);
    EXPECT_NEAR(item.get_subtotal().get_yuan(), 0.0, 1e-9);
}

TEST(OrderItem, ConstructorAndGetters) {
    OrderItem item(1, "宫保鸡丁", Money(25.0), 3);
    EXPECT_EQ(item.get_dish_id(), 1);
    EXPECT_STREQ(item.get_dish_name(), "宫保鸡丁");
    EXPECT_NEAR(item.get_price().get_yuan(), 25.0, 1e-9);
    EXPECT_EQ(item.get_quantity(), 3);
}

TEST(OrderItem, SubtotalIsPriceTimesQuantity) {
    OrderItem item(2, "米饭", Money(2.5), 4);
    EXPECT_NEAR(item.get_subtotal().get_yuan(), 10.0, 1e-9);
}

TEST(OrderItem, SubtotalWithZeroQuantity) {
    OrderItem item(3, "赠品", Money(9.9), 0);
    EXPECT_NEAR(item.get_subtotal().get_yuan(), 0.0, 1e-9);
}

TEST(OrderItem, SubtotalWithFractionalPrice) {
    OrderItem item(4, "奶茶", Money(7.5), 2);
    EXPECT_NEAR(item.get_subtotal().get_yuan(), 15.0, 1e-9);
}
