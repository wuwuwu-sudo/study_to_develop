#pragma once

#include <sqlite3.h>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <string>

namespace infrastructure {
namespace database {

class ConnectionPool {
public:
    explicit ConnectionPool(const std::string& db_path, int pool_size = 16, int timeout_ms = 5000);
    ~ConnectionPool();

    ConnectionPool(const ConnectionPool&) = delete;
    ConnectionPool& operator=(const ConnectionPool&) = delete;

    // 获取一个空闲连接，若池已满则阻塞等待最多 timeout_ms 毫秒
    // 超时或池已关闭时返回 nullptr
    sqlite3* getConnection();

    // 归还连接。连接无效时自动替换为新连接
    void releaseConnection(sqlite3* conn);

    // 关闭所有空闲连接并停止获取新连接（析构函数会自动调用）
    void closeAll();

    // 连接池配置的总连接数
    int poolSize() const;

    // 当前空闲连接数
    int available() const;

    // 当前已借出的连接数
    int inUse() const;

private:
    sqlite3* createConnection();
    void resetConnection(sqlite3* conn);
    bool isValid(sqlite3* conn) const;

    std::string db_path_;
    int pool_size_;
    int timeout_ms_;
    bool shutdown_;

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::queue<sqlite3*> pool_;
    int in_use_ = 0;
};

} // namespace database
} // namespace infrastructure
