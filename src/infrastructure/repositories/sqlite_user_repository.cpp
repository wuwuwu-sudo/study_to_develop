#include "sqlite_user_repository.h"
#include "infrastructure/common/logger.h"
#include <sqlite3.h>

namespace infrastructure::repositories {

using infrastructure::common::Logger;

SqliteUserRepository::SqliteUserRepository(infrastructure::database::DbManager& db) : db_(db) {}

std::optional<User> SqliteUserRepository::find_by_id(int user_id) {
    auto conn = db_.get_connection();
    if (!conn) {
        Logger::instance().error("Failed to get database connection");
        return std::nullopt;
    }

    sqlite3* handle = conn->handle();
    const char* sql = "SELECT id, username, password_hash, active FROM users WHERE id = ?;";
    
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(handle, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        Logger::instance().error("Failed to prepare statement: " + std::string(sqlite3_errmsg(handle)));
        return std::nullopt;
    }

    sqlite3_bind_int(stmt, 1, user_id);

    User user;
    bool found = false;
    
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        int id = sqlite3_column_int(stmt, 0);
        const char* username = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        const char* password_hash = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        int active = sqlite3_column_int(stmt, 3);
        
        user = User(id, username ? username : "", password_hash ? password_hash : "", active != 0);
        found = true;
    }

    sqlite3_finalize(stmt);
    return found ? std::optional<User>(user) : std::nullopt;
}

std::optional<User> SqliteUserRepository::find_by_username(const std::string& username) {
    auto conn = db_.get_connection();
    if (!conn) {
        Logger::instance().error("Failed to get database connection");
        return std::nullopt;
    }

    sqlite3* handle = conn->handle();
    const char* sql = "SELECT id, username, password_hash, active FROM users WHERE username = ?;";
    
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(handle, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        Logger::instance().error("Failed to prepare statement: " + std::string(sqlite3_errmsg(handle)));
        return std::nullopt;
    }

    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_STATIC);

    User user;
    bool found = false;
    
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        int id = sqlite3_column_int(stmt, 0);
        const char* uname = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        const char* password_hash = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        int active = sqlite3_column_int(stmt, 3);
        
        user = User(id, uname ? uname : "", password_hash ? password_hash : "", active != 0);
        found = true;
    }

    sqlite3_finalize(stmt);
    return found ? std::optional<User>(user) : std::nullopt;
}

int SqliteUserRepository::save(const User& user) {
    auto conn = db_.get_connection();
    if (!conn) {
        Logger::instance().error("Failed to get database connection");
        return -1;
    }

    sqlite3* handle = conn->handle();
    const char* sql = "INSERT OR REPLACE INTO users (id, username, password_hash, active) VALUES (?, ?, ?, ?);";
    
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(handle, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        Logger::instance().error("Failed to prepare statement: " + std::string(sqlite3_errmsg(handle)));
        return -1;
    }

    // id 为 0 时绑定 NULL，让 AUTOINCREMENT 自动生成新 id
    if (user.get_id() != 0) {
        sqlite3_bind_int(stmt, 1, user.get_id());
    } else {
        sqlite3_bind_null(stmt, 1);
    }
    sqlite3_bind_text(stmt, 2, user.get_username().c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, user.get_password_hash().c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 4, user.is_active() ? 1 : 0);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        Logger::instance().error("Failed to save user: " + std::string(sqlite3_errmsg(handle)));
        return -1;
    }

    // 如果是新插入的记录，获取插入的ID
    int user_id = user.get_id();
    if (user_id == 0) {
        user_id = sqlite3_last_insert_rowid(handle);
    }
    
    return user_id;
}

}  // namespace infrastructure::repositories
