#pragma once

// ============================================================
// tests/mocks/mock_repositories.h
// 应用层服务依赖的仓储接口的内存版实现（测试替身）
// 用于在不依赖 SQLite 的情况下测试 AuthService / DishService /
// OrderService 的逻辑。
// ============================================================

#include <optional>
#include <string>
#include <vector>

#include "domain/models/dish.h"
#include "domain/models/merchant.h"
#include "domain/models/order.h"
#include "domain/models/user.h"
#include "infrastructure/repositories/interfaces/i_dish_repository.h"
#include "infrastructure/repositories/interfaces/i_merchant_repository.h"
#include "infrastructure/repositories/interfaces/i_order_repository.h"
#include "infrastructure/repositories/interfaces/i_user_repository.h"

namespace test_mocks {

// ---------------- 用户仓储 Mock ----------------
class MockUserRepository : public infrastructure::repositories::IUserRepository {
public:
    std::optional<User> find_by_id(int user_id) override {
        (void)user_id;
        return std::nullopt;
    }

    std::optional<User> find_by_username(const std::string& username) override {
        (void)username;
        return std::nullopt;
    }

    int save(const User& user) override {
        (void)user;
        return 1;
    }
};

// ---------------- 商家仓储 Mock ----------------
class MockMerchantRepository : public infrastructure::repositories::IMerchantRepository {
public:
    std::optional<Merchant> find_by_id(int merchant_id) override {
        (void)merchant_id;
        return std::nullopt;
    }

    std::optional<Merchant> find_by_username(const std::string& username) override {
        (void)username;
        return std::nullopt;
    }

    int save(const Merchant& merchant) override {
        (void)merchant;
        return 1;
    }
};

// ---------------- 菜品仓储 Mock ----------------
class MockDishRepository : public infrastructure::repositories::IDishRepository {
public:
    std::optional<Dish> find_by_id(int dish_id) override {
        (void)dish_id;
        return std::nullopt;
    }

    std::vector<Dish> find_by_merchant(int merchant_id) override {
        (void)merchant_id;
        return {};
    }

    int save(const Dish& dish) override {
        (void)dish;
        return 1;
    }

    bool update(const Dish& dish) override {
        (void)dish;
        return true;
    }
};

// ---------------- 订单仓储 Mock ----------------
class MockOrderRepository : public infrastructure::repositories::IOrderRepository {
public:
    std::optional<Order> find_by_id(int order_id) override {
        (void)order_id;
        return std::nullopt;
    }

    int save(const Order& order) override {
        (void)order;
        return 1;
    }

    bool update(const Order& order) override {
        (void)order;
        return true;
    }

    std::vector<Order> find_by_user(int user_id) override {
        (void)user_id;
        return {};
    }

    std::vector<Order> find_by_merchant(int merchant_id) override {
        (void)merchant_id;
        return {};
    }
};

}  // namespace test_mocks
