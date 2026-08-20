#pragma once

struct sqlite3;

namespace infrastructure::database {

class ConnectionPool;

class DbConnection {
public:
    explicit DbConnection(sqlite3* handle, ConnectionPool* pool = nullptr);
    ~DbConnection();

    // 禁止拷贝
    DbConnection(const DbConnection&) = delete;
    DbConnection& operator=(const DbConnection&) = delete;

    // 支持移动语义，便于作为 unique_ptr / 返回值使用
    DbConnection(DbConnection&& other) noexcept;
    DbConnection& operator=(DbConnection&& other) noexcept;

    // 获取底层 sqlite3 句柄
    sqlite3* handle() const;

    // 显式归还连接（析构函数会自动调用）
    void release();

    // 是否有效
    explicit operator bool() const { return handle_ != nullptr; }

private:
    sqlite3* handle_ = nullptr;
    ConnectionPool* pool_ = nullptr;
};

}  // namespace infrastructure::database
