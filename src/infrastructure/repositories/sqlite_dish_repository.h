#pragma once

#include <optional>
#include <string>
#include <vector>
#include "infrastructure/repositories/interfaces/i_dish_repository.h"
#include "infrastructure/database/db_manager.h"

namespace infrastructure::repositories {

class SqliteDishRepository : public IDishRepository {
public:
    explicit SqliteDishRepository(infrastructure::database::DbManager& db);

    std::optional<Dish> find_by_id(int dish_id) override;
    std::vector<Dish> find_by_merchant(int merchant_id) override;
    int save(const Dish& dish) override;
    bool update(const Dish& dish) override;
    bool set_available(int dish_id, bool available) override;
    bool delete_dish(int dish_id) override;
    std::vector<Dish> find_paged(int merchant_id, int page, int page_size) override;
    int count_by_merchant(int merchant_id) override;

private:
    infrastructure::database::DbManager& db_;
};

}  // namespace infrastructure::repositories
