// ============================================================
// tests/domain/test_order_status_machine.cpp
// 对应: src/domain/services/order_status_machine.{h,cpp}
// ============================================================
#include "test_framework.h"

#include "domain/services/order_status_machine.h"

#define STATUS(v) static_cast<int>(OrderStatus::v)

TEST(OrderStatusMachine, PendingAllowedTransitions) {
    EXPECT_TRUE(OrderStatusMachine::can_transition(OrderStatus::PENDING, OrderStatus::CONFIRMED));
    EXPECT_TRUE(OrderStatusMachine::can_transition(OrderStatus::PENDING, OrderStatus::CANCELLED));
}

TEST(OrderStatusMachine, PendingRejectedTransitions) {
    EXPECT_FALSE(OrderStatusMachine::can_transition(OrderStatus::PENDING, OrderStatus::DELIVERING));
    EXPECT_FALSE(OrderStatusMachine::can_transition(OrderStatus::PENDING, OrderStatus::DELIVERED));
    EXPECT_FALSE(OrderStatusMachine::can_transition(OrderStatus::PENDING, OrderStatus::PENDING));
}

TEST(OrderStatusMachine, ConfirmedAllowedTransitions) {
    EXPECT_TRUE(OrderStatusMachine::can_transition(OrderStatus::CONFIRMED, OrderStatus::DELIVERING));
    EXPECT_TRUE(OrderStatusMachine::can_transition(OrderStatus::CONFIRMED, OrderStatus::CANCELLED));
}

TEST(OrderStatusMachine, ConfirmedRejectedTransitions) {
    EXPECT_FALSE(OrderStatusMachine::can_transition(OrderStatus::CONFIRMED, OrderStatus::PENDING));
    EXPECT_FALSE(OrderStatusMachine::can_transition(OrderStatus::CONFIRMED, OrderStatus::DELIVERED));
    EXPECT_FALSE(OrderStatusMachine::can_transition(OrderStatus::CONFIRMED, OrderStatus::CONFIRMED));
}

TEST(OrderStatusMachine, DeliveringAllowedTransitions) {
    EXPECT_TRUE(OrderStatusMachine::can_transition(OrderStatus::DELIVERING, OrderStatus::DELIVERED));
    EXPECT_TRUE(OrderStatusMachine::can_transition(OrderStatus::DELIVERING, OrderStatus::CANCELLED));
}

TEST(OrderStatusMachine, DeliveringRejectedTransitions) {
    EXPECT_FALSE(OrderStatusMachine::can_transition(OrderStatus::DELIVERING, OrderStatus::PENDING));
    EXPECT_FALSE(OrderStatusMachine::can_transition(OrderStatus::DELIVERING, OrderStatus::CONFIRMED));
    EXPECT_FALSE(OrderStatusMachine::can_transition(OrderStatus::DELIVERING, OrderStatus::DELIVERING));
}

TEST(OrderStatusMachine, DeliveredIsTerminal) {
    EXPECT_FALSE(OrderStatusMachine::can_transition(OrderStatus::DELIVERED, OrderStatus::CANCELLED));
    EXPECT_FALSE(OrderStatusMachine::can_transition(OrderStatus::DELIVERED, OrderStatus::PENDING));
    EXPECT_FALSE(OrderStatusMachine::can_transition(OrderStatus::DELIVERED, OrderStatus::CONFIRMED));
    EXPECT_FALSE(OrderStatusMachine::can_transition(OrderStatus::DELIVERED, OrderStatus::DELIVERING));
    EXPECT_FALSE(OrderStatusMachine::can_transition(OrderStatus::DELIVERED, OrderStatus::DELIVERED));
}

TEST(OrderStatusMachine, CancelledIsTerminal) {
    EXPECT_FALSE(OrderStatusMachine::can_transition(OrderStatus::CANCELLED, OrderStatus::PENDING));
    EXPECT_FALSE(OrderStatusMachine::can_transition(OrderStatus::CANCELLED, OrderStatus::CONFIRMED));
    EXPECT_FALSE(OrderStatusMachine::can_transition(OrderStatus::CANCELLED, OrderStatus::DELIVERING));
    EXPECT_FALSE(OrderStatusMachine::can_transition(OrderStatus::CANCELLED, OrderStatus::DELIVERED));
    EXPECT_FALSE(OrderStatusMachine::can_transition(OrderStatus::CANCELLED, OrderStatus::CANCELLED));
}

// 完整转换矩阵一致性校验：枚举中的每个值都应被正确处理
TEST(OrderStatusMachine, FullMatrixIsConsistent) {
    const OrderStatus all[] = {
        OrderStatus::PENDING,
        OrderStatus::CONFIRMED,
        OrderStatus::DELIVERING,
        OrderStatus::DELIVERED,
        OrderStatus::CANCELLED,
    };

    // 期望的邻接表（PENDING -> CONFIRMED/CANCELLED 等）
    const bool expected[5][5] = {
        // to: PENDING CONFIRMED DELIVERING DELIVERED CANCELLED
        {false, true,  false,    false,     true},    // from PENDING
        {false, false, true,     false,     true},    // from CONFIRMED
        {false, false, false,    true,      true},    // from DELIVERING
        {false, false, false,    false,     false},   // from DELIVERED
        {false, false, false,    false,     false},   // from CANCELLED
    };

    for (int i = 0; i < 5; ++i) {
        for (int j = 0; j < 5; ++j) {
            const bool actual = OrderStatusMachine::can_transition(all[i], all[j]);
            if (actual != expected[i][j]) {
                FAIL();  // 输出更友好的诊断
            }
        }
    }
    SUCCEED();
}

#undef STATUS
