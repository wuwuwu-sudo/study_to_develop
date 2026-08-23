#pragma once

#include <optional>
#include <vector>
#include "domain/models/order.h"

namespace infrastructure::repositories {

class IOrderRepository {
public:
    virtual ~IOrderRepository() = default;

    virtual std::optional<Order> find_by_id(int order_id) = 0;
    virtual int save(const Order& order) = 0;
    virtual bool update(const Order& order) = 0;
    // 乐观锁条件更新：仅当当前 status == expected_status 才更新。
    // 返回 1=成功 / 0=乐观锁冲突（影响 0 行）/ -1=数据库错误。
    virtual int update_optimistic(const Order& order, OrderStatus expected_status) = 0;
    virtual std::vector<Order> find_by_user(int user_id) = 0;
    virtual std::vector<Order> find_by_merchant(int merchant_id) = 0;
};

}  // namespace infrastructure::repositories
