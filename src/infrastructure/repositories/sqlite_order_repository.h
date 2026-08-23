#pragma once

#include <optional>
#include <string>
#include <vector>
#include "infrastructure/repositories/interfaces/i_order_repository.h"
#include "infrastructure/database/db_manager.h"

namespace infrastructure::repositories {

class SqliteOrderRepository : public IOrderRepository {
public:
    explicit SqliteOrderRepository(infrastructure::database::DbManager& db);

    std::optional<Order> find_by_id(int order_id) override;
    int save(const Order& order) override;
    bool update(const Order& order) override;
    int update_optimistic(const Order& order, OrderStatus expected_status) override;
    std::vector<Order> find_by_user(int user_id) override;
    std::vector<Order> find_by_merchant(int merchant_id) override;

private:
    infrastructure::database::DbManager& db_;
};

}  // namespace infrastructure::repositories
