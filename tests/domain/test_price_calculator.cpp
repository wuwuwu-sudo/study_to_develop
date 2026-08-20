// ============================================================
// tests/domain/test_price_calculator.cpp
// 对应: src/domain/services/price_calculator.{h,cpp}
// 注意：
//   - apply_discounts() 目前为占位实现（原样返回）
//   - calculate_delivery_fee() 目前恒返回 5.0
// ============================================================
#include "test_framework.h"

#include "domain/models/merchant.h"
#include "domain/models/order.h"
#include "domain/models/order_item.h"
#include "domain/models/user.h"
#include "domain/services/price_calculator.h"

TEST(PriceCalculator, EmptySubtotalIsZero) {
    const std::vector<OrderItem> items;
    EXPECT_NEAR(PriceCalculator::calculate_subtotal(items).get_yuan(), 0.0, 1e-9);
}

TEST(PriceCalculator, SubtotalSumsItems) {
    std::vector<OrderItem> items;
    items.emplace_back(1, "A", Money(10.0), 2);   // 20.0
    items.emplace_back(2, "B", Money(3.5), 1);    //  3.5
    items.emplace_back(3, "C", Money(1.0), 5);    //  5.0
    EXPECT_NEAR(PriceCalculator::calculate_subtotal(items).get_yuan(), 28.5, 1e-9);
}

TEST(PriceCalculator, ApplyDiscountsReturnsTotalUnchanged) {
    // 占位实现：暂无折扣逻辑
    User user(1, "u", "h");
    Merchant merchant(1, "m", "店", "地址");
    EXPECT_NEAR(PriceCalculator::apply_discounts(Money(100.0), user, merchant).get_yuan(),
                100.0, 1e-9);
}

TEST(PriceCalculator, DeliveryFeeIsFive) {
    // 占位实现：默认 5 元配送费
    User user(1, "u", "h");
    EXPECT_NEAR(PriceCalculator::calculate_delivery_fee(Money(50.0), user).get_yuan(),
                5.0, 1e-9);
}

TEST(PriceCalculator, OrderTotalIsSubtotalPlusDelivery) {
    Order order(1, 2);
    order.add_item(OrderItem(1, "A", Money(10.0), 2));   // 20.0
    order.add_item(OrderItem(2, "B", Money(3.5), 1));    //  3.5
    User user(1, "u", "h");
    Merchant merchant(1, "m", "店", "地址");

    // subtotal=23.5, discount=0, delivery=5.0 => 28.5
    Money total = PriceCalculator::calculate_order_total(order, user, merchant);
    EXPECT_NEAR(total.get_yuan(), 28.5, 1e-9);
}

TEST(PriceCalculator, EmptyOrderTotalIsOnlyDeliveryFee) {
    Order order(1, 2);
    User user(1, "u", "h");
    Merchant merchant(1, "m", "店", "地址");
    Money total = PriceCalculator::calculate_order_total(order, user, merchant);
    EXPECT_NEAR(total.get_yuan(), 5.0, 1e-9);
}
