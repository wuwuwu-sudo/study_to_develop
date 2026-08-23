#include "sqlite_order_repository.h"
#include "infrastructure/common/logger.h"
#include <sqlite3.h>
#include <utility>

namespace infrastructure::repositories {

using infrastructure::common::Logger;

namespace {

// 将数据库中的订单状态字符串解析为 OrderStatus
OrderStatus parse_status(const char* str) {
    if (str == nullptr) {
        return OrderStatus::PENDING;
    }
    const std::string s(str);
    if (s == "PENDING") return OrderStatus::PENDING;
    if (s == "CONFIRMED") return OrderStatus::CONFIRMED;
    if (s == "DELIVERING") return OrderStatus::DELIVERING;
    if (s == "DELIVERED") return OrderStatus::DELIVERED;
    if (s == "CANCELLED") return OrderStatus::CANCELLED;
    Logger::instance().warn("Unknown order status string: " + s);
    return OrderStatus::PENDING;
}

// 将 OrderStatus 转换为数据库存储的字符串
const char* status_to_string(OrderStatus status) {
    switch (status) {
        case OrderStatus::PENDING:    return "PENDING";
        case OrderStatus::CONFIRMED:  return "CONFIRMED";
        case OrderStatus::DELIVERING: return "DELIVERING";
        case OrderStatus::DELIVERED:  return "DELIVERED";
        case OrderStatus::CANCELLED:  return "CANCELLED";
    }
    return "PENDING";
}

// 从结果集当前行读取订单头部信息。
// 列顺序固定为：id, user_id, merchant_id, status, total, address, remark
Order read_order_header(sqlite3_stmt* stmt) {
    int id = sqlite3_column_int(stmt, 0);
    int user_id = sqlite3_column_int(stmt, 1);
    int merchant_id = sqlite3_column_int(stmt, 2);
    const char* status_str = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
    const char* address = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));

    Order order(id, user_id, merchant_id);
    order.set_status(parse_status(status_str));
    order.set_address(address ? address : "");
    return order;
}

// 加载指定订单的全部订单项到 order 中
bool load_order_items(sqlite3* handle, int order_id, Order& order) {
    const char* sql = "SELECT dish_id, dish_name, price, quantity FROM order_items WHERE order_id = ?;";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(handle, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        Logger::instance().error("Failed to prepare statement: " + std::string(sqlite3_errmsg(handle)));
        return false;
    }

    sqlite3_bind_int(stmt, 1, order_id);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int dish_id = sqlite3_column_int(stmt, 0);
        const char* dish_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        double price = sqlite3_column_double(stmt, 2);
        int quantity = sqlite3_column_int(stmt, 3);
        order.add_item(OrderItem(dish_id, dish_name ? dish_name : "", Money(price), quantity));
    }
    sqlite3_finalize(stmt);
    return true;
}

}  // namespace

SqliteOrderRepository::SqliteOrderRepository(infrastructure::database::DbManager& db) : db_(db) {}

std::optional<Order> SqliteOrderRepository::find_by_id(int order_id) {
    auto conn = db_.get_connection();
    if (!conn) {
        Logger::instance().error("Failed to get database connection");
        return std::nullopt;
    }

    sqlite3* handle = conn->handle();

    // 查询订单信息
    const char* sql_order =
        "SELECT id, user_id, merchant_id, status, total, address, remark FROM orders WHERE id = ?;";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(handle, sql_order, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        Logger::instance().error("Failed to prepare statement: " + std::string(sqlite3_errmsg(handle)));
        return std::nullopt;
    }

    sqlite3_bind_int(stmt, 1, order_id);

    std::optional<Order> result = std::nullopt;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        Order order = read_order_header(stmt);
        // 加载订单项
        if (!load_order_items(handle, order_id, order)) {
            sqlite3_finalize(stmt);
            return std::nullopt;
        }
        result = order;
    }

    sqlite3_finalize(stmt);
    return result;
}

int SqliteOrderRepository::save(const Order& order) {
    auto conn = db_.get_write_connection();  // 阶段1 单写串行化门
    if (!conn) {
        Logger::instance().error("Failed to get database connection");
        return -1;
    }

    sqlite3* handle = conn->handle();

    // 订单与订单项需要一起写入，使用事务保证原子性
    if (sqlite3_exec(handle, "BEGIN;", nullptr, nullptr, nullptr) != SQLITE_OK) {
        Logger::instance().error("Failed to begin transaction: " + std::string(sqlite3_errmsg(handle)));
        return -1;
    }

    const char* sql = "INSERT INTO orders (user_id, merchant_id, status, total, address, remark) VALUES (?, ?, ?, ?, ?, ?);";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(handle, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        Logger::instance().error("Failed to prepare statement: " + std::string(sqlite3_errmsg(handle)));
        sqlite3_exec(handle, "ROLLBACK;", nullptr, nullptr, nullptr);
        return -1;
    }

    sqlite3_bind_int(stmt, 1, order.get_user_id());
    sqlite3_bind_int(stmt, 2, order.get_merchant_id());

    // 将 OrderStatus 转换为数据库存储的字符串
    sqlite3_bind_text(stmt, 3, status_to_string(order.get_status()), -1, SQLITE_STATIC);

    sqlite3_bind_double(stmt, 4, order.get_total().get_yuan());
    sqlite3_bind_text(stmt, 5, order.get_address().c_str(), -1, SQLITE_STATIC);  // address
    sqlite3_bind_text(stmt, 6, "", -1, SQLITE_STATIC);  // remark

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        Logger::instance().error("Failed to save order: " + std::string(sqlite3_errmsg(handle)));
        sqlite3_exec(handle, "ROLLBACK;", nullptr, nullptr, nullptr);
        return -1;
    }

    int order_id = static_cast<int>(sqlite3_last_insert_rowid(handle));

    // 保存订单项
    for (const auto& item : order.get_items()) {
        const char* sql_item =
            "INSERT INTO order_items (order_id, dish_id, dish_name, price, quantity) VALUES (?, ?, ?, ?, ?);";
        sqlite3_stmt* stmt_item = nullptr;
        rc = sqlite3_prepare_v2(handle, sql_item, -1, &stmt_item, nullptr);
        if (rc != SQLITE_OK) {
            Logger::instance().error("Failed to prepare item statement: " + std::string(sqlite3_errmsg(handle)));
            sqlite3_exec(handle, "ROLLBACK;", nullptr, nullptr, nullptr);
            return -1;
        }

        sqlite3_bind_int(stmt_item, 1, order_id);
        sqlite3_bind_int(stmt_item, 2, item.get_dish_id());
        sqlite3_bind_text(stmt_item, 3, item.get_dish_name().c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_double(stmt_item, 4, item.get_price().get_yuan());
        sqlite3_bind_int(stmt_item, 5, item.get_quantity());

        rc = sqlite3_step(stmt_item);
        sqlite3_finalize(stmt_item);
        if (rc != SQLITE_DONE) {
            Logger::instance().error("Failed to save order item: " + std::string(sqlite3_errmsg(handle)));
            sqlite3_exec(handle, "ROLLBACK;", nullptr, nullptr, nullptr);
            return -1;
        }
    }

    if (sqlite3_exec(handle, "COMMIT;", nullptr, nullptr, nullptr) != SQLITE_OK) {
        Logger::instance().error("Failed to commit transaction: " + std::string(sqlite3_errmsg(handle)));
        sqlite3_exec(handle, "ROLLBACK;", nullptr, nullptr, nullptr);
        return -1;
    }

    return order_id;
}

bool SqliteOrderRepository::update(const Order& order) {
    auto conn = db_.get_write_connection();  // 阶段1 单写串行化门
    if (!conn) {
        Logger::instance().error("Failed to get database connection");
        return false;
    }

    sqlite3* handle = conn->handle();
    const char* sql =
        "UPDATE orders SET status = ?, total = ?, address = ?, remark = ? WHERE id = ?;";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(handle, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        Logger::instance().error("Failed to prepare statement: " + std::string(sqlite3_errmsg(handle)));
        return false;
    }

    sqlite3_bind_text(stmt, 1, status_to_string(order.get_status()), -1, SQLITE_STATIC);
    sqlite3_bind_double(stmt, 2, order.get_total().get_yuan());
    sqlite3_bind_text(stmt, 3, order.get_address().c_str(), -1, SQLITE_STATIC);  // address
    sqlite3_bind_text(stmt, 4, "", -1, SQLITE_STATIC);  // remark
    sqlite3_bind_int(stmt, 5, order.get_id());

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        Logger::instance().error("Failed to update order: " + std::string(sqlite3_errmsg(handle)));
        return false;
    }
    return sqlite3_changes(handle) > 0;
}

int SqliteOrderRepository::update_optimistic(const Order& order, OrderStatus expected_status) {
    auto conn = db_.get_write_connection();  // 阶段1 单写串行化门
    if (!conn) {
        Logger::instance().error("Failed to get database connection");
        return -1;
    }

    sqlite3* handle = conn->handle();
    // 乐观锁条件更新：仅当当前 status == 期望旧状态才更新；影响 0 行 = 并发冲突
    const char* sql =
        "UPDATE orders SET status = ?, total = ?, address = ?, remark = ? "
        "WHERE id = ? AND status = ?;";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(handle, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        Logger::instance().error("Failed to prepare statement: " + std::string(sqlite3_errmsg(handle)));
        return -1;
    }

    sqlite3_bind_text(stmt, 1, status_to_string(order.get_status()), -1, SQLITE_STATIC);
    sqlite3_bind_double(stmt, 2, order.get_total().get_yuan());
    sqlite3_bind_text(stmt, 3, order.get_address().c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, "", -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 5, order.get_id());
    sqlite3_bind_text(stmt, 6, status_to_string(expected_status), -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        Logger::instance().error("Failed to update order (optimistic): " +
                                 std::string(sqlite3_errmsg(handle)));
        return -1;
    }
    return sqlite3_changes(handle) > 0 ? 1 : 0;
}

std::vector<Order> SqliteOrderRepository::find_by_user(int user_id) {
    std::vector<Order> orders;
    auto conn = db_.get_connection();
    if (!conn) {
        Logger::instance().error("Failed to get database connection");
        return orders;
    }

    sqlite3* handle = conn->handle();
    const char* sql =
        "SELECT id, user_id, merchant_id, status, total, address, remark "
        "FROM orders WHERE user_id = ? ORDER BY id DESC;";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(handle, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        Logger::instance().error("Failed to prepare statement: " + std::string(sqlite3_errmsg(handle)));
        return orders;
    }

    sqlite3_bind_int(stmt, 1, user_id);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Order order = read_order_header(stmt);
        load_order_items(handle, order.get_id(), order);
        orders.push_back(std::move(order));
    }

    sqlite3_finalize(stmt);
    return orders;
}

std::vector<Order> SqliteOrderRepository::find_by_merchant(int merchant_id) {
    std::vector<Order> orders;
    auto conn = db_.get_connection();
    if (!conn) {
        Logger::instance().error("Failed to get database connection");
        return orders;
    }

    sqlite3* handle = conn->handle();
    const char* sql =
        "SELECT id, user_id, merchant_id, status, total, address, remark "
        "FROM orders WHERE merchant_id = ? ORDER BY id DESC;";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(handle, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        Logger::instance().error("Failed to prepare statement: " + std::string(sqlite3_errmsg(handle)));
        return orders;
    }

    sqlite3_bind_int(stmt, 1, merchant_id);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Order order = read_order_header(stmt);
        load_order_items(handle, order.get_id(), order);
        orders.push_back(std::move(order));
    }

    sqlite3_finalize(stmt);
    return orders;
}

}  // namespace infrastructure::repositories
