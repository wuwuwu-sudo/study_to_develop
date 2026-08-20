// ============================================================
// tests/application/test_dish_service.cpp
// 对应: src/application/dish_service.{h,cpp}
// 注意：
//   - create_dish() 目前为占位实现（恒返回 id=1）
//   - get_dishes() 目前恒返回空列表
//   - set_available()/soft_delete() 恒返回 true
// ============================================================
#include "test_framework.h"

#include <memory>

#include "application/dish_service.h"
#include "mocks/mock_repositories.h"

using application::DishService;
using test_mocks::MockDishRepository;

namespace {

struct DishFixture {
    std::shared_ptr<MockDishRepository> repo;
    DishService service;

    DishFixture()
        : repo(std::make_shared<MockDishRepository>())
        , service(repo) {}
};

}  // namespace

TEST(DishService, CreateDishAssignsPlaceholderId) {
    DishFixture f;
    DishDto dto;
    dto.merchant_id = 5;
    dto.name = "红烧肉";
    dto.price = 38.0;
    dto.category = "热菜";

    DishDto created = f.service.create_dish(dto);
    EXPECT_EQ(created.id, 1);
    EXPECT_EQ(created.merchant_id, 5);
    EXPECT_STREQ(created.name, "红烧肉");
    EXPECT_NEAR(created.price, 38.0, 1e-9);
}

TEST(DishService, UpdateDishReturnsInputUnchanged) {
    DishFixture f;
    DishDto dto;
    dto.id = 10;
    dto.name = "宫保鸡丁";
    dto.available = false;

    DishDto updated = f.service.update_dish(dto);
    EXPECT_EQ(updated.id, 10);
    EXPECT_STREQ(updated.name, "宫保鸡丁");
    EXPECT_FALSE(updated.available);
}

TEST(DishService, GetDishesReturnsEmptyForNow) {
    DishFixture f;
    // 占位实现：暂无查询逻辑
    std::vector<DishDto> dishes = f.service.get_dishes(5);
    EXPECT_EQ(dishes.size(), static_cast<size_t>(0));
}

TEST(DishService, SetAvailableReturnsTrue) {
    DishFixture f;
    EXPECT_TRUE(f.service.set_available(1, true));
    EXPECT_TRUE(f.service.set_available(2, false));
}

TEST(DishService, SoftDeleteReturnsTrue) {
    DishFixture f;
    EXPECT_TRUE(f.service.soft_delete(1));
}
