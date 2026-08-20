#pragma once

#include <optional>
#include <vector>
#include "domain/models/dish.h"

namespace infrastructure::repositories {

class IDishRepository {
public:
    virtual ~IDishRepository() = default;

    virtual std::optional<Dish> find_by_id(int dish_id) = 0;
    virtual std::vector<Dish> find_by_merchant(int merchant_id) = 0;
    virtual int save(const Dish& dish) = 0;
    virtual bool update(const Dish& dish) = 0;

    // 更新菜品的上架/下架状态
    virtual bool set_available(int dish_id, bool available) = 0;

    // 删除菜品（软删除：deleted = 1）
    virtual bool delete_dish(int dish_id) = 0;

    // 分页查询某商家的未删除菜品；page 从 1 开始
    virtual std::vector<Dish> find_paged(int merchant_id, int page, int page_size) = 0;

    // 统计某商家的未删除菜品总数（用于分页）
    virtual int count_by_merchant(int merchant_id) = 0;
};

}  // namespace infrastructure::repositories
