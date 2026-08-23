#include "sqlite_dish_repository.h"
#include "infrastructure/common/logger.h"
#include <sqlite3.h>

namespace infrastructure::repositories {

using infrastructure::common::Logger;

namespace {

// 从结果集当前行读取一个 Dish。
// 列顺序固定为：id, merchant_id, name, price, category, description, available, deleted, version
Dish read_dish_from_row(sqlite3_stmt* stmt) {
    int id = sqlite3_column_int(stmt, 0);
    int merchant_id = sqlite3_column_int(stmt, 1);
    const char* name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
    double price = sqlite3_column_double(stmt, 3);
    const char* category = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
    const char* description = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
    int available = sqlite3_column_int(stmt, 6);
    int deleted = sqlite3_column_int(stmt, 7);
    int version = sqlite3_column_int(stmt, 8);

    Dish dish(id, merchant_id,
              name ? name : "",
              Money(price),
              category ? category : "",
              description ? description : "");
    if (!available) {
        dish.set_available(false);
    }
    if (deleted) {
        dish.soft_delete();
    }
    dish.set_version(version);
    return dish;
}

}  // namespace

SqliteDishRepository::SqliteDishRepository(infrastructure::database::DbManager& db) : db_(db) {}

std::optional<Dish> SqliteDishRepository::find_by_id(int dish_id) {
    auto conn = db_.get_connection();
    if (!conn) {
        Logger::instance().error("Failed to get database connection");
        return std::nullopt;
    }

    sqlite3* handle = conn->handle();
    const char* sql = "SELECT id, merchant_id, name, price, category, description, available, deleted, version FROM dishes WHERE id = ?;";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(handle, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        Logger::instance().error("Failed to prepare statement: " + std::string(sqlite3_errmsg(handle)));
        return std::nullopt;
    }

    sqlite3_bind_int(stmt, 1, dish_id);

    Dish dish;
    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        dish = read_dish_from_row(stmt);
        found = true;
    }

    sqlite3_finalize(stmt);
    return found ? std::optional<Dish>(dish) : std::nullopt;
}

std::vector<Dish> SqliteDishRepository::find_by_merchant(int merchant_id) {
    std::vector<Dish> dishes;
    auto conn = db_.get_connection();
    if (!conn) {
        Logger::instance().error("Failed to get database connection");
        return dishes;
    }

    sqlite3* handle = conn->handle();
    const char* sql = "SELECT id, merchant_id, name, price, category, description, available, deleted, version "
                      "FROM dishes WHERE merchant_id = ? AND deleted = 0;";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(handle, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        Logger::instance().error("Failed to prepare statement: " + std::string(sqlite3_errmsg(handle)));
        return dishes;
    }

    sqlite3_bind_int(stmt, 1, merchant_id);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        dishes.push_back(read_dish_from_row(stmt));
    }

    sqlite3_finalize(stmt);
    return dishes;
}

int SqliteDishRepository::save(const Dish& dish) {
    auto conn = db_.get_write_connection();  // 阶段1 单写串行化门
    if (!conn) {
        Logger::instance().error("Failed to get database connection");
        return -1;
    }

    sqlite3* handle = conn->handle();
    const char* sql =
        "INSERT INTO dishes (merchant_id, name, price, category, description, available, deleted) "
        "VALUES (?, ?, ?, ?, ?, ?, 0);";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(handle, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        Logger::instance().error("Failed to prepare statement: " + std::string(sqlite3_errmsg(handle)));
        return -1;
    }

    sqlite3_bind_int(stmt, 1, dish.get_merchant_id());
    sqlite3_bind_text(stmt, 2, dish.get_name().c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_double(stmt, 3, dish.get_price().get_yuan());
    sqlite3_bind_text(stmt, 4, dish.get_category().c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 5, dish.get_description().c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 6, dish.is_available() ? 1 : 0);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        Logger::instance().error("Failed to save dish: " + std::string(sqlite3_errmsg(handle)));
        return -1;
    }

    // 返回数据库自增生成的 id
    return static_cast<int>(sqlite3_last_insert_rowid(handle));
}

bool SqliteDishRepository::update(const Dish& dish) {
    auto conn = db_.get_write_connection();  // 阶段1 单写串行化门
    if (!conn) {
        Logger::instance().error("Failed to get database connection");
        return false;
    }

    sqlite3* handle = conn->handle();
    const char* sql =
        "UPDATE dishes SET name = ?, price = ?, category = ?, description = ?, available = ? "
        "WHERE id = ? AND deleted = 0;";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(handle, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        Logger::instance().error("Failed to prepare statement: " + std::string(sqlite3_errmsg(handle)));
        return false;
    }

    sqlite3_bind_text(stmt, 1, dish.get_name().c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_double(stmt, 2, dish.get_price().get_yuan());
    sqlite3_bind_text(stmt, 3, dish.get_category().c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, dish.get_description().c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 5, dish.is_available() ? 1 : 0);
    sqlite3_bind_int(stmt, 6, dish.get_id());

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        Logger::instance().error("Failed to update dish: " + std::string(sqlite3_errmsg(handle)));
        return false;
    }
    return sqlite3_changes(handle) > 0;
}

int SqliteDishRepository::update_optimistic(const Dish& dish, int expected_version) {
    auto conn = db_.get_write_connection();  // 阶段1 单写串行化门
    if (!conn) {
        Logger::instance().error("Failed to get database connection");
        return -1;
    }

    sqlite3* handle = conn->handle();
    // 乐观锁版本列：仅当当前 version == 期望版本才更新并 version+1；0 行 = 并发冲突
    const char* sql =
        "UPDATE dishes SET name = ?, price = ?, category = ?, description = ?, "
        "available = ?, version = version + 1 "
        "WHERE id = ? AND deleted = 0 AND version = ?;";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(handle, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        Logger::instance().error("Failed to prepare statement: " + std::string(sqlite3_errmsg(handle)));
        return -1;
    }

    sqlite3_bind_text(stmt, 1, dish.get_name().c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_double(stmt, 2, dish.get_price().get_yuan());
    sqlite3_bind_text(stmt, 3, dish.get_category().c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, dish.get_description().c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 5, dish.is_available() ? 1 : 0);
    sqlite3_bind_int(stmt, 6, dish.get_id());
    sqlite3_bind_int(stmt, 7, expected_version);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        Logger::instance().error("Failed to update dish (optimistic): " +
                                 std::string(sqlite3_errmsg(handle)));
        return -1;
    }
    return sqlite3_changes(handle) > 0 ? 1 : 0;
}

bool SqliteDishRepository::set_available(int dish_id, bool available) {
    auto conn = db_.get_write_connection();  // 阶段1 单写串行化门
    if (!conn) {
        Logger::instance().error("Failed to get database connection");
        return false;
    }

    sqlite3* handle = conn->handle();
    // 幂等条件更新：仅当当前 available != 目标值才更新；已处于目标状态（0 行）也视为成功。
    // 切换语义下并发设置天然一致（最后一次生效），无丢失更新风险。
    const char* sql =
        "UPDATE dishes SET available = ? WHERE id = ? AND deleted = 0 AND available != ?;";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(handle, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        Logger::instance().error("Failed to prepare statement: " + std::string(sqlite3_errmsg(handle)));
        return false;
    }

    sqlite3_bind_int(stmt, 1, available ? 1 : 0);
    sqlite3_bind_int(stmt, 2, dish_id);
    sqlite3_bind_int(stmt, 3, available ? 1 : 0);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        Logger::instance().error("Failed to set dish availability: " + std::string(sqlite3_errmsg(handle)));
        return false;
    }
    return true;  // 0 行 = 已处于目标状态（幂等成功）
}

bool SqliteDishRepository::delete_dish(int dish_id) {
    auto conn = db_.get_write_connection();  // 阶段1 单写串行化门
    if (!conn) {
        Logger::instance().error("Failed to get database connection");
        return false;
    }

    sqlite3* handle = conn->handle();
    // 软删除：置 deleted = 1，保留订单历史中的菜品数据
    const char* sql = "UPDATE dishes SET deleted = 1 WHERE id = ? AND deleted = 0;";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(handle, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        Logger::instance().error("Failed to prepare statement: " + std::string(sqlite3_errmsg(handle)));
        return false;
    }

    sqlite3_bind_int(stmt, 1, dish_id);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        Logger::instance().error("Failed to delete dish: " + std::string(sqlite3_errmsg(handle)));
        return false;
    }
    return sqlite3_changes(handle) > 0;
}

std::vector<Dish> SqliteDishRepository::find_paged(int merchant_id, int page, int page_size) {
    std::vector<Dish> dishes;
    if (page < 1) {
        page = 1;
    }
    if (page_size < 1) {
        page_size = 10;
    }
    if (page_size > 100) {
        page_size = 100;  // 防止单页过大
    }

    auto conn = db_.get_connection();
    if (!conn) {
        Logger::instance().error("Failed to get database connection");
        return dishes;
    }

    sqlite3* handle = conn->handle();
    const char* sql =
        "SELECT id, merchant_id, name, price, category, description, available, deleted, version "
        "FROM dishes WHERE merchant_id = ? AND deleted = 0 "
        "ORDER BY id LIMIT ? OFFSET ?;";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(handle, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        Logger::instance().error("Failed to prepare statement: " + std::string(sqlite3_errmsg(handle)));
        return dishes;
    }

    long long offset = static_cast<long long>(page - 1) * page_size;
    sqlite3_bind_int(stmt, 1, merchant_id);
    sqlite3_bind_int(stmt, 2, page_size);
    sqlite3_bind_int64(stmt, 3, offset);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        dishes.push_back(read_dish_from_row(stmt));
    }

    sqlite3_finalize(stmt);
    return dishes;
}

int SqliteDishRepository::count_by_merchant(int merchant_id) {
    auto conn = db_.get_connection();
    if (!conn) {
        Logger::instance().error("Failed to get database connection");
        return 0;
    }

    sqlite3* handle = conn->handle();
    const char* sql = "SELECT COUNT(*) FROM dishes WHERE merchant_id = ? AND deleted = 0;";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(handle, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        Logger::instance().error("Failed to prepare statement: " + std::string(sqlite3_errmsg(handle)));
        return 0;
    }

    sqlite3_bind_int(stmt, 1, merchant_id);

    int count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = sqlite3_column_int(stmt, 0);
    }

    sqlite3_finalize(stmt);
    return count;
}

}  // namespace infrastructure::repositories
