#pragma once

#include <memory>
#include <string>

#include "infrastructure/database/db_connection.h"

namespace infrastructure::database {

class ConnectionPool;

class DbManager {
public:
    static DbManager& instance();

    // 初始化连接池（可多次调用，会先关闭旧连接池）
    bool initialize(const std::string& db_path, int pool_size = 16, int timeout_ms = 5000);

    // 关闭连接池
    void shutdown();

    // 获取一个数据库连接。返回的 unique_ptr 析构时自动归还连接池（RAII），
    // 调用方无需也无法手动归还，也不会拿到裸句柄。
    std::unique_ptr<DbConnection> get_connection();

    // 阶段1 单写串行化：获取一个持有进程内写锁的连接（写路径专用）。
    // 持有期间本进程所有写操作互斥排队，减少写-写争用与 SQLITE_BUSY；
    // 读路径仍走 get_connection() 保持并发。
    std::unique_ptr<DbConnection> get_write_connection();

    // ====== 连接池管理函数 ======
    int pool_size() const;         // 配置的总连接数
    int available_connections() const;  // 当前空闲连接数
    int in_use_connections() const;     // 当前借出的连接数

    void run_migrations();

private:
    DbManager();
    ~DbManager();

    DbManager(const DbManager&) = delete;
    DbManager& operator=(const DbManager&) = delete;

    std::unique_ptr<ConnectionPool> pool_;
};

}  // namespace infrastructure::database
