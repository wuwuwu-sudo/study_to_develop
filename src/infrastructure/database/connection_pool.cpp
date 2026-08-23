#include "connection_pool.h"

#include <chrono>
#include <stdexcept>

namespace infrastructure {
namespace database {

ConnectionPool::ConnectionPool(const std::string& db_path, int pool_size, int timeout_ms)
    : db_path_(db_path),
      pool_size_(pool_size),
      timeout_ms_(timeout_ms),
      shutdown_(false) {
    if (pool_size_ <= 0) {
        throw std::invalid_argument("pool_size must be positive");
    }

    std::lock_guard<std::mutex> lock(mutex_);
    for (int i = 0; i < pool_size_; ++i) {
        sqlite3* conn = createConnection();
        if (!conn) {
            throw std::runtime_error("failed to create sqlite connection: " + db_path_);
        }
        pool_.push(conn);
    }
}

ConnectionPool::~ConnectionPool() {
    closeAll();
}

sqlite3* ConnectionPool::createConnection() {
    sqlite3* conn = nullptr;
    int rc = sqlite3_open(db_path_.c_str(), &conn);
    if (rc != SQLITE_OK) {
        if (conn != nullptr) {
            sqlite3_close(conn);
        }
        return nullptr;
    }

    sqlite3_busy_timeout(conn, timeout_ms_);
    sqlite3_exec(conn, "PRAGMA foreign_keys = ON;", nullptr, nullptr, nullptr);

    // 三实例共享同一 SQLite 文件，默认 rollback journal 下读锁会阻塞写锁，
    // 高并发写入极易触发 SQLITE_BUSY。开启 WAL 后读写并发不再互斥：
    //   - journal_mode=WAL  持久化到库文件，读写分离到主库+WAL
    //   - synchronous=NORMAL 单次写只需刷 WAL 文件，写延迟大幅降低
    sqlite3_exec(conn, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
    sqlite3_exec(conn, "PRAGMA synchronous=NORMAL;", nullptr, nullptr, nullptr);

    return conn;
}

sqlite3* ConnectionPool::getConnection() {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms_), [this]() {
        return !pool_.empty() || shutdown_;
    });

    if (shutdown_ || pool_.empty()) {
        return nullptr;
    }

    sqlite3* conn = pool_.front();
    pool_.pop();
    ++in_use_;
    return conn;
}

void ConnectionPool::releaseConnection(sqlite3* conn) {
    if (conn == nullptr) {
        return;
    }

    if (!isValid(conn)) {
        sqlite3_close(conn);
        conn = createConnection();
        if (conn == nullptr) {
            std::lock_guard<std::mutex> lock(mutex_);
            --in_use_;
            shutdown_ = true;
            cv_.notify_all();
            return;
        }
    }

    // 清除遗留的 prepared statements 和未完成事务
    resetConnection(conn);

    {
        std::lock_guard<std::mutex> lock(mutex_);
        --in_use_;
        if (shutdown_) {
            sqlite3_close(conn);
            return;
        }
        pool_.push(conn);
    }
    cv_.notify_one();
}

void ConnectionPool::resetConnection(sqlite3* conn) {
    sqlite3_stmt* stmt = sqlite3_next_stmt(conn, nullptr);
    while (stmt != nullptr) {
        sqlite3_stmt* next = sqlite3_next_stmt(conn, stmt);
        sqlite3_finalize(stmt);
        stmt = next;
    }
    sqlite3_exec(conn, "ROLLBACK;", nullptr, nullptr, nullptr);
}

bool ConnectionPool::isValid(sqlite3* conn) const {
    if (conn == nullptr) {
        return false;
    }
    return sqlite3_exec(conn, "SELECT 1;", nullptr, nullptr, nullptr) == SQLITE_OK;
}

void ConnectionPool::closeAll() {
    std::queue<sqlite3*> connections;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        shutdown_ = true;
        connections.swap(pool_);
    }

    while (!connections.empty()) {
        sqlite3* conn = connections.front();
        connections.pop();
        if (conn != nullptr) {
            sqlite3_close(conn);
        }
    }
    cv_.notify_all();
}

int ConnectionPool::poolSize() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return pool_size_;
}

int ConnectionPool::available() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return static_cast<int>(pool_.size());
}

int ConnectionPool::inUse() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return in_use_;
}

std::unique_lock<std::mutex> ConnectionPool::lock_for_write() {
    return std::unique_lock<std::mutex>(write_mutex_);
}

} // namespace database
} // namespace infrastructure