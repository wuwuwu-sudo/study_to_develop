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
    virtual std::vector<Order> find_by_user(int user_id) = 0;
    virtual std::vector<Order> find_by_merchant(int merchant_id) = 0;
};

}  // namespace infrastructure::repositories
