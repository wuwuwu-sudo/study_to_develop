// ============================================================
// tests/application/test_order_service.cpp
// 对应: src/application/order_service.{h,cpp}
// 注意：
//   - create_order() 目前为占位实现（恒返回 1）
//   - update_order_status() 恒返回 true
//   - get_orders_for_user()/get_orders_for_merchant() 恒返回空列表
// ============================================================
#include "test_framework.h"

#include <memory>

#include "application/order_service.h"
#include "mocks/mock_repositories.h"

using application::OrderService;
using test_mocks::MockDishRepository;
using test_mocks::MockMerchantRepository;
using test_mocks::MockOrderRepository;
using test_mocks::MockUserRepository;

namespace {

struct OrderFixture {
    std::shared_ptr<MockOrderRepository> order_repo;
    std::shared_ptr<MockDishRepository> dish_repo;
    std::shared_ptr<MockUserRepository> user_repo;
    std::shared_ptr<MockMerchantRepository> merchant_repo;
    OrderService service;

    OrderFixture()
        : order_repo(std::make_shared<MockOrderRepository>())
        , dish_repo(std::make_shared<MockDishRepository>())
        , user_repo(std::make_shared<MockUserRepository>())
        , merchant_repo(std::make_shared<MockMerchantRepository>())
        , service(order_repo, dish_repo, user_repo, merchant_repo) {}
};

}  // namespace

TEST(OrderService, CreateOrderReturnsPlaceholderId) {
    OrderFixture f;
    std::vector<OrderItemDto> items;
    OrderItemDto item;
    item.dish_id = 1;
    item.quantity = 2;
    items.push_back(item);

    // 占位实现：目前不真正落库，恒返回 1
    int order_id = f.service.create_order(1, 2, items);
    EXPECT_EQ(order_id, 1);
}

TEST(OrderService, CreateOrderWithEmptyItems) {
    OrderFixture f;
    EXPECT_EQ(f.service.create_order(1, 2, {}), 1);
}

TEST(OrderService, UpdateOrderStatusReturnsTrue) {
    OrderFixture f;
    EXPECT_TRUE(f.service.update_order_status(1, 2, OrderStatus::CONFIRMED));
    EXPECT_TRUE(f.service.update_order_status(1, 2, OrderStatus::CANCELLED));
}

TEST(OrderService, GetOrdersForUserEmptyForNow) {
    OrderFixture f;
    std::vector<OrderDto> orders = f.service.get_orders_for_user(1);
    EXPECT_EQ(orders.size(), static_cast<size_t>(0));
}

TEST(OrderService, GetOrdersForMerchantEmptyForNow) {
    OrderFixture f;
    std::vector<OrderDto> orders = f.service.get_orders_for_merchant(2);
    EXPECT_EQ(orders.size(), static_cast<size_t>(0));
}
