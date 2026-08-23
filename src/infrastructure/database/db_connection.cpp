#include "infrastructure/database/db_connection.h"

#include <sqlite3.h>
#include <utility>

#include "infrastructure/database/connection_pool.h"

namespace infrastructure::database {

DbConnection::DbConnection(sqlite3* handle, ConnectionPool* pool,
                           std::unique_lock<std::mutex> write_lock)
    : handle_(handle), pool_(pool), write_lock_(std::move(write_lock)) {}

DbConnection::DbConnection(DbConnection&& other) noexcept
    : handle_(other.handle_),
      pool_(other.pool_),
      write_lock_(std::move(other.write_lock_)) {
    other.handle_ = nullptr;
    other.pool_ = nullptr;
}

DbConnection& DbConnection::operator=(DbConnection&& other) noexcept {
    if (this != &other) {
        release();
        handle_ = other.handle_;
        pool_ = other.pool_;
        write_lock_ = std::move(other.write_lock_);
        other.handle_ = nullptr;
        other.pool_ = nullptr;
    }
    return *this;
}

DbConnection::~DbConnection() {
    release();
}

sqlite3* DbConnection::handle() const {
    return handle_;
}

void DbConnection::release() {
    if (handle_ == nullptr) {
        return;
    }

    if (pool_ != nullptr) {
        pool_->releaseConnection(handle_);
    } else {
        sqlite3_close(handle_);
    }

    handle_ = nullptr;
    pool_ = nullptr;
}

}  // namespace infrastructure::database
