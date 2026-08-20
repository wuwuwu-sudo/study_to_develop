// ============================================================
// tests/domain/test_order.cpp
// 对应: src/domain/models/order.{h,cpp}
// 注意：订单状态转换遵循 OrderStatusMachine 定义的规则
// ============================================================
#include "test_framework.h"

#include "domain/models/order.h"
#include "domain/models/order_item.h"
#include "domain/value_objects/order_status.h"

TEST(Order, DefaultConstruct) {
    Order o;
    EXPECT_EQ(o.get_user_id(), 0);
    EXPECT_EQ(o.get_merchant_id(), 0);
    EXPECT_EQ(static_cast<int>(o.get_status()), static_cast<int>(OrderStatus::PENDING));
    EXPECT_EQ(o.get_items().size(), static_cast<size_t>(0));
    EXPECT_NEAR(o.get_total().get_yuan(), 0.0, 1e-9);
}

TEST(Order, ConstructWithIds) {
    Order o(3, 5);
    EXPECT_EQ(o.get_user_id(), 3);
    EXPECT_EQ(o.get_merchant_id(), 5);
    EXPECT_EQ(static_cast<int>(o.get_status()), static_cast<int>(OrderStatus::PENDING));
}

TEST(Order, AddItemComputesTotal) {
    Order o(1, 2);
    o.add_item(OrderItem(1, "A", Money(12.5), 2));  // 25.0
    EXPECT_NEAR(o.get_total().get_yuan(), 25.0, 1e-9);

    o.add_item(OrderItem(2, "B", Money(1.0), 3));   // +3.0 => 28.0
    EXPECT_NEAR(o.get_total().get_yuan(), 28.0, 1e-9);
    EXPECT_EQ(o.get_items().size(), static_cast<size_t>(2));
}

TEST(Order, AddItemKeepsItemData) {
    Order o(1, 2);
    o.add_item(OrderItem(7, "面", Money(18.0), 1));
    const auto& items = o.get_items();
    EXPECT_EQ(items.size(), static_cast<size_t>(1));
    EXPECT_EQ(items[0].get_dish_id(), 7);
    EXPECT_STREQ(items[0].get_dish_name(), "面");
}

TEST(Order, HappyPathTransitions) {
    Order o(1, 2);
    o.confirm();
    EXPECT_EQ(static_cast<int>(o.get_status()), static_cast<int>(OrderStatus::CONFIRMED));

    o.start_delivery();
    EXPECT_EQ(static_cast<int>(o.get_status()), static_cast<int>(OrderStatus::DELIVERING));

    o.complete_delivery();
    EXPECT_EQ(static_cast<int>(o.get_status()), static_cast<int>(OrderStatus::DELIVERED));
}

TEST(Order, CancelFromPending) {
    Order o(1, 2);
    o.cancel();
    EXPECT_EQ(static_cast<int>(o.get_status()), static_cast<int>(OrderStatus::CANCELLED));
}

TEST(Order, InvalidTransitionIgnored) {
    // PENDING 不能直接跳到 DELIVERING
    Order o(1, 2);
    o.start_delivery();
    EXPECT_EQ(static_cast<int>(o.get_status()), static_cast<int>(OrderStatus::PENDING));
}

TEST(Order, TerminalStateRejectsFurtherTransitions) {
    // CANCELLED 是终态
    Order o(1, 2);
    o.cancel();
    o.confirm();
    EXPECT_EQ(static_cast<int>(o.get_status()), static_cast<int>(OrderStatus::CANCELLED));

    // DELIVERED 是终态
    Order o2(1, 2);
    o2.confirm();
    o2.start_delivery();
    o2.complete_delivery();
    o2.cancel();
    EXPECT_EQ(static_cast<int>(o2.get_status()), static_cast<int>(OrderStatus::DELIVERED));
}

TEST(Order, RepeatedAddItemAccumulatesTotal) {
    Order o(1, 2);
    o.add_item(OrderItem(1, "A", Money(10.0), 1));
    o.add_item(OrderItem(1, "A", Money(10.0), 1));
    EXPECT_NEAR(o.get_total().get_yuan(), 20.0, 1e-9);
}
