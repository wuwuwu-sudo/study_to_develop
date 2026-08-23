#include "sqlite_merchant_repository.h"
#include "infrastructure/common/logger.h"
#include <sqlite3.h>

namespace infrastructure::repositories {

using infrastructure::common::Logger;

SqliteMerchantRepository::SqliteMerchantRepository(infrastructure::database::DbManager& db) : db_(db) {}

std::optional<Merchant> SqliteMerchantRepository::find_by_id(int merchant_id) {
    auto conn = db_.get_connection();
    if (!conn) {
        Logger::instance().error("Failed to get database connection");
        return std::nullopt;
    }

    sqlite3* handle = conn->handle();
    const char* sql =
        "SELECT id, username, password_hash, shop_name, address, is_open FROM merchants WHERE id = ?;";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(handle, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        Logger::instance().error("Failed to prepare statement: " + std::string(sqlite3_errmsg(handle)));
        return std::nullopt;
    }

    sqlite3_bind_int(stmt, 1, merchant_id);

    Merchant merchant;
    bool found = false;

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        int id = sqlite3_column_int(stmt, 0);
        const char* username = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        const char* password_hash = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        const char* shop_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        const char* address = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        int is_open = sqlite3_column_int(stmt, 5);

        merchant = Merchant(id,
                            username ? username : "",
                            shop_name ? shop_name : "",
                            address ? address : "",
                            is_open != 0);
        merchant.set_password_hash(password_hash ? password_hash : "");
        found = true;
    }

    sqlite3_finalize(stmt);
    return found ? std::optional<Merchant>(merchant) : std::nullopt;
}

std::optional<Merchant> SqliteMerchantRepository::find_by_username(const std::string& username) {
    auto conn = db_.get_connection();
    if (!conn) {
        Logger::instance().error("Failed to get database connection");
        return std::nullopt;
    }

    sqlite3* handle = conn->handle();
    const char* sql =
        "SELECT id, username, password_hash, shop_name, address, is_open FROM merchants WHERE username = ?;";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(handle, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        Logger::instance().error("Failed to prepare statement: " + std::string(sqlite3_errmsg(handle)));
        return std::nullopt;
    }

    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_STATIC);

    Merchant merchant;
    bool found = false;

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        int id = sqlite3_column_int(stmt, 0);
        const char* uname = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        const char* password_hash = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        const char* shop_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        const char* address = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        int is_open = sqlite3_column_int(stmt, 5);

        merchant = Merchant(id,
                            uname ? uname : "",
                            shop_name ? shop_name : "",
                            address ? address : "",
                            is_open != 0);
        merchant.set_password_hash(password_hash ? password_hash : "");
        found = true;
    }

    sqlite3_finalize(stmt);
    return found ? std::optional<Merchant>(merchant) : std::nullopt;
}

int SqliteMerchantRepository::save(const Merchant& merchant) {
    auto conn = db_.get_write_connection();  // 阶段1 单写串行化门
    if (!conn) {
        Logger::instance().error("Failed to get database connection");
        return -1;
    }

    sqlite3* handle = conn->handle();
    const char* sql =
        "INSERT OR REPLACE INTO merchants (id, username, password_hash, shop_name, address, is_open) "
        "VALUES (?, ?, ?, ?, ?, ?);";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(handle, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        Logger::instance().error("Failed to prepare statement: " + std::string(sqlite3_errmsg(handle)));
        return -1;
    }

    // id 为 0 时绑定 NULL，让 AUTOINCREMENT 自动生成新 id
    if (merchant.get_id() != 0) {
        sqlite3_bind_int(stmt, 1, merchant.get_id());
    } else {
        sqlite3_bind_null(stmt, 1);
    }
    sqlite3_bind_text(stmt, 2, merchant.get_username().c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, merchant.get_password_hash().c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, merchant.get_shop_name().c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 5, merchant.get_address().c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 6, merchant.is_open() ? 1 : 0);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        Logger::instance().error("Failed to save merchant: " + std::string(sqlite3_errmsg(handle)));
        return -1;
    }

    int merchant_id = merchant.get_id();
    if (merchant_id == 0) {
        merchant_id = sqlite3_last_insert_rowid(handle);
    }

    return merchant_id;
}

bool SqliteMerchantRepository::update_open_status(int merchant_id, bool open) {
    auto conn = db_.get_write_connection();  // 阶段1 单写串行化门
    if (!conn) {
        Logger::instance().error("Failed to get database connection");
        return false;
    }

    sqlite3* handle = conn->handle();
    // 幂等条件更新：仅当当前 is_open != 目标值才更新；已处于目标状态（0 行）也视为成功。
    // 避免整行 INSERT OR REPLACE 覆盖并发修改的其他字段（丢失更新）。
    const char* sql =
        "UPDATE merchants SET is_open = ? WHERE id = ? AND is_open != ?;";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(handle, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        Logger::instance().error("Failed to prepare statement: " + std::string(sqlite3_errmsg(handle)));
        return false;
    }

    sqlite3_bind_int(stmt, 1, open ? 1 : 0);
    sqlite3_bind_int(stmt, 2, merchant_id);
    sqlite3_bind_int(stmt, 3, open ? 1 : 0);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        Logger::instance().error("Failed to update merchant open status: " +
                                 std::string(sqlite3_errmsg(handle)));
        return false;
    }
    return true;  // 0 行 = 已处于目标状态（幂等成功）
}

std::vector<Merchant> SqliteMerchantRepository::find_open_merchants() {
    std::vector<Merchant> merchants;
    auto conn = db_.get_connection();
    if (!conn) {
        Logger::instance().error("Failed to get database connection");
        return merchants;
    }

    sqlite3* handle = conn->handle();
    const char* sql =
        "SELECT id, username, password_hash, shop_name, address, is_open "
        "FROM merchants WHERE is_open = 1 ORDER BY id;";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(handle, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        Logger::instance().error("Failed to prepare statement: " + std::string(sqlite3_errmsg(handle)));
        return merchants;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int id = sqlite3_column_int(stmt, 0);
        const char* username = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        const char* password_hash = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        const char* shop_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        const char* address = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        int is_open = sqlite3_column_int(stmt, 5);

        Merchant merchant(id,
                          username ? username : "",
                          shop_name ? shop_name : "",
                          address ? address : "",
                          is_open != 0);
        merchant.set_password_hash(password_hash ? password_hash : "");
        merchants.push_back(std::move(merchant));
    }

    sqlite3_finalize(stmt);
    return merchants;
}

}  // namespace infrastructure::repositories
