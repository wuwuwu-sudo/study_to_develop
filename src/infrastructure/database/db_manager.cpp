#include "infrastructure/database/db_manager.h"

#include <sqlite3.h>

#include "infrastructure/common/logger.h"
#include "infrastructure/database/connection_pool.h"

namespace infrastructure::database {

// Logger 位于 infrastructure::common 命名空间
using infrastructure::common::Logger;

DbManager& DbManager::instance() {
    static DbManager manager;
    return manager;
}

DbManager::DbManager() = default;

DbManager::~DbManager() {
    shutdown();
}

bool DbManager::initialize(const std::string& db_path, int pool_size, int timeout_ms) {
    shutdown();

    try {
        pool_ = std::make_unique<ConnectionPool>(db_path, pool_size, timeout_ms);
    } catch (const std::exception& e) {
        Logger::instance().error(std::string("Connection pool initialization failed: ") + e.what());
        return false;
    }

    return true;
}

void DbManager::shutdown() {
    if (pool_) {
        pool_->closeAll();
        pool_.reset();
    }
}

std::unique_ptr<DbConnection> DbManager::get_connection() {
    if (!pool_) {
        return nullptr;
    }

    sqlite3* handle = pool_->getConnection();
    if (handle == nullptr) {
        return nullptr;
    }

    // 用 unique_ptr 托管 DbConnection；析构时通过 DbConnection 自动归还连接池
    return std::make_unique<DbConnection>(handle, pool_.get());
}

int DbManager::pool_size() const {
    return pool_ ? pool_->poolSize() : 0;
}

int DbManager::available_connections() const {
    return pool_ ? pool_->available() : 0;
}

int DbManager::in_use_connections() const {
    return pool_ ? pool_->inUse() : 0;
}

void DbManager::run_migrations() {
    Logger::instance().info("Database migrations pending");
}

}  // namespace infrastructure::database
