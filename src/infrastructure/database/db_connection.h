#pragma once

#include <mutex>

struct sqlite3;

namespace infrastructure::database {

class ConnectionPool;

class DbConnection {
public:
    // write_lock：阶段1 单写串行化门——传入后该连接整个生命周期持有写锁，
    // 使本进程内写路径互斥（默认空锁 = 普通读连接）。
    explicit DbConnection(sqlite3* handle, ConnectionPool* pool = nullptr,
                          std::unique_lock<std::mutex> write_lock = {});
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
    // 阶段1 写串行化门：非空表示本连接持有进程内写锁（析构/移动时随对象释放）
    std::unique_lock<std::mutex> write_lock_;
};

}  // namespace infrastructure::database
