#include "init_db.h"
#include "infrastructure/database/db_manager.h"
#include "infrastructure/database/db_connection.h"
#include "infrastructure/common/logger.h"
#include <sqlite3.h>
#include <algorithm>   
#include <memory>
#include <vector>

namespace infrastructure::database {

using infrastructure::common::Logger;

namespace {

// 校验表结构，返回 true 表示结构与期望一致
bool validate_table(sqlite3* handle, const char* table_name,
                    const std::vector<std::string>& expected_columns) {
    // 1. 校验表是否存在
    std::string check_sql =
        "SELECT name FROM sqlite_master WHERE type='table' AND name='" +
        std::string(table_name) + "';";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(handle, check_sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        Logger::instance().error(std::string("Failed to check table ") + table_name);
        return false;
    }

    bool exists = (sqlite3_step(stmt) == SQLITE_ROW);
    sqlite3_finalize(stmt);

    if (!exists) {
        Logger::instance().error("Table missing after migration: " + std::string(table_name));
        return false;
    }

    // 2. 校验列结构
    std::string pragma_sql = "PRAGMA table_info(" + std::string(table_name) + ");";
    std::vector<std::string> actual_columns;

    if (sqlite3_prepare_v2(handle, pragma_sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* col_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        if (col_name) {
            actual_columns.emplace_back(col_name);
        }
    }
    sqlite3_finalize(stmt);

    // 期望的列在 actual_columns 中都存在即可（允许表中多出列，保持向后兼容）
    for (const auto& expected : expected_columns) {
        if (std::find(actual_columns.begin(), actual_columns.end(), expected) == actual_columns.end()) {
            Logger::instance().error("Column missing in table " + std::string(table_name) +
                                     ": " + expected);
            return false;
        }
    }

    return true;
}

}  // namespace

void init_database() {
    auto& db = DbManager::instance();

    // ============================================================
    // 校验 1：连接池是否已初始化
    // ============================================================
    if (db.pool_size() <= 0) {
        Logger::instance().error(
            "Connection pool not initialized. "
            "Call DbManager::instance().initialize() before init_database().");
        return;
    }

    // RAII：作用域结束自动归还连接池
    auto conn = db.get_connection();

    if (!conn) {
        Logger::instance().error("Failed to get database connection from pool");
        return;
    }

    sqlite3* handle = conn->handle();
    char* errmsg = nullptr;

    auto exec_sql = [&](const char* sql) -> bool {
        int rc = sqlite3_exec(handle, sql, nullptr, nullptr, &errmsg);
        if (rc != SQLITE_OK) {
            Logger::instance().error("SQL failed: " + std::string(sql) + " | " +
                                     (errmsg ? errmsg : ""));
            if (errmsg) {
                sqlite3_free(errmsg);
                errmsg = nullptr;
            }
            return false;
        }
        return true;
    };

    // ============================================================
    // 校验 2：数据库完整性
    // ============================================================
    {
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(handle, "PRAGMA integrity_check;", -1, &stmt, nullptr) != SQLITE_OK) {
            Logger::instance().error("Failed to run integrity_check");
            return;
        }

        bool ok = false;
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* result =
                reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            if (result && std::string(result) == "ok") {
                ok = true;
            }
        }
        sqlite3_finalize(stmt);

        if (!ok) {
            Logger::instance().error("Database integrity check failed");
            return;
        }
    }

    // ============================================================
    // 校验 3：外键约束开启
    // ============================================================
    {
        sqlite3_stmt* stmt = nullptr;
        bool fk_on = false;
        if (sqlite3_prepare_v2(handle, "PRAGMA foreign_keys;", -1, &stmt, nullptr) == SQLITE_OK) {
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                fk_on = sqlite3_column_int(stmt, 0) != 0;
            }
            sqlite3_finalize(stmt);
        }
        if (!fk_on) {
            Logger::instance().warn("Foreign key enforcement is DISABLED");
        }
    }

    // ============================================================
    // 建表 SQL（与之前相同）
    // ============================================================
    const char* create_users =
        "CREATE TABLE IF NOT EXISTS users ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "username TEXT UNIQUE NOT NULL,"
        "password_hash TEXT NOT NULL,"
        "role TEXT NOT NULL DEFAULT 'CUSTOMER',"
        "active INTEGER DEFAULT 1,"
        "created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP"
        ");";

    const char* create_merchants =
        "CREATE TABLE IF NOT EXISTS merchants ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "username TEXT UNIQUE NOT NULL,"
        "password_hash TEXT NOT NULL DEFAULT '',"
        "shop_name TEXT NOT NULL,"
        "address TEXT NOT NULL,"
        "is_open INTEGER DEFAULT 1"
        ");";

    const char* create_dishes =
        "CREATE TABLE IF NOT EXISTS dishes ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "merchant_id INTEGER NOT NULL,"
        "name TEXT NOT NULL,"
        "price REAL NOT NULL,"
        "category TEXT NOT NULL,"
        "description TEXT,"
        "available INTEGER DEFAULT 1,"
        "deleted INTEGER DEFAULT 0,"
        "FOREIGN KEY (merchant_id) REFERENCES merchants(id)"
        ");";

    const char* create_orders =
        "CREATE TABLE IF NOT EXISTS orders ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "user_id INTEGER NOT NULL,"
        "merchant_id INTEGER NOT NULL,"
        "status TEXT NOT NULL DEFAULT 'PENDING',"
        "total REAL NOT NULL DEFAULT 0,"
        "address TEXT NOT NULL,"
        "remark TEXT,"
        "created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,"
        "FOREIGN KEY (user_id) REFERENCES users(id),"
        "FOREIGN KEY (merchant_id) REFERENCES merchants(id)"
        ");";

    const char* create_order_items =
        "CREATE TABLE IF NOT EXISTS order_items ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "order_id INTEGER NOT NULL,"
        "dish_id INTEGER NOT NULL,"
        "dish_name TEXT NOT NULL,"
        "price REAL NOT NULL,"
        "quantity INTEGER NOT NULL,"
        "FOREIGN KEY (order_id) REFERENCES orders(id),"
        "FOREIGN KEY (dish_id) REFERENCES dishes(id)"
        ");";

    // 事务包裹所有 DDL，保证原子性
    if (!exec_sql("BEGIN;")) {
        return;
    }

    bool ok = exec_sql(create_users) &&
              exec_sql(create_merchants) &&
              exec_sql(create_dishes) &&
              exec_sql(create_orders) &&
              exec_sql(create_order_items);

    if (!ok) {
        exec_sql("ROLLBACK;");
        Logger::instance().error("Database initialization failed, rolled back");
        return;
    }

    if (!exec_sql("COMMIT;")) {
        return;
    }

    // ============================================================
    // 兼容旧库：若 merchants 表缺少 password_hash 列则补充（迁移）
    // ============================================================
    {
        bool has_password_hash = false;
        const char* pragma_sql = "PRAGMA table_info(merchants);";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(handle, pragma_sql, -1, &stmt, nullptr) == SQLITE_OK) {
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                const char* col = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
                if (col && std::string(col) == "password_hash") {
                    has_password_hash = true;
                }
            }
            sqlite3_finalize(stmt);
        }
        if (!has_password_hash) {
            const char* alter_sql =
                "ALTER TABLE merchants ADD COLUMN password_hash TEXT NOT NULL DEFAULT '';";
            if (sqlite3_exec(handle, alter_sql, nullptr, nullptr, nullptr) != SQLITE_OK) {
                Logger::instance().error("Failed to add password_hash column to merchants");
                return;
            }
            Logger::instance().info("Migrated merchants: added password_hash column");
        }
    }

    // ============================================================
    // 校验 4：表结构与期望是否一致
    // ============================================================
    struct TableSchema {
        const char* name;
        std::vector<std::string> columns;
    };

    std::vector<TableSchema> schemas = {
        {"users", {"id", "username", "password_hash", "role", "active", "created_at"}},
        {"merchants", {"id", "username", "password_hash", "shop_name", "address", "is_open"}},
        {"dishes", {"id", "merchant_id", "name", "price", "category", "description", "available", "deleted"}},
        {"orders", {"id", "user_id", "merchant_id", "status", "total", "address", "remark", "created_at"}},
        {"order_items", {"id", "order_id", "dish_id", "dish_name", "price", "quantity"}},
    };

    for (const auto& schema : schemas) {
        if (!validate_table(handle, schema.name, schema.columns)) {
            Logger::instance().error(
                "Schema validation failed for table: " + std::string(schema.name));
            return;
        }
    }

    // ============================================================
    // 创建索引 ==
    // ============================================================
    const char* create_indexes[] = {
        "CREATE INDEX IF NOT EXISTS idx_orders_user_id ON orders(user_id);",
        "CREATE INDEX IF NOT EXISTS idx_orders_merchant_id ON orders(merchant_id);",
        "CREATE INDEX IF NOT EXISTS idx_orders_status ON orders(status);",
        "CREATE INDEX IF NOT EXISTS idx_dishes_merchant_id ON dishes(merchant_id);",
        "CREATE INDEX IF NOT EXISTS idx_dishes_category ON dishes(category);"
    };

    for (const char* sql : create_indexes) {
        if (!exec_sql(sql)) {
            Logger::instance().warn("Failed to create index");
        }
    }

    Logger::instance().info("Database initialized and validated successfully");
    // conn 为 unique_ptr，作用域结束时自动归还连接池
}

}  // namespace infrastructure::database
