#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include "infrastructure/session/session_store.h"

namespace infrastructure::session {

class SessionManager {
public:
    static SessionManager& instance();

    void initialize(int ttl_seconds);

    // 替换底层存储实现（默认内存存储）。通常用于测试或切换存储后端，
    // 应在 start_cleanup_loop() 之前调用。
    void set_store(std::shared_ptr<SessionStore> store);

    // 立即批量清理所有过期会话，返回被清理的会话数量。
    std::size_t cleanup_expired();

    // 启动后台线程，每隔 interval_seconds 秒执行一次 cleanup_expired()。
    // 已在运行时调用无副作用；interval_seconds <= 0 时按 1 秒处理。
    void start_cleanup_loop(int interval_seconds);
    // 停止后台清理线程（幂等，内部会 join）。
    void stop_cleanup_loop();

    // 当前存储中的会话总数（含过期未清理的）。
    std::size_t session_count() const;

    std::string create_session(int user_id, int merchant_id);
    bool validate_session(const std::string& session_id);
    // 返回会话信息（含 user_id/merchant_id）；无效或不存在时返回 nullopt
    std::optional<SessionInfo> find(const std::string& session_id);
    void destroy_session(const std::string& session_id);
    static std::string generate_hex_id(std::size_t length);

private:
    SessionManager();
    ~SessionManager();

    int ttl_seconds_ = 3600;
    std::shared_ptr<SessionStore> store_;

    // 后台清理线程相关状态
    std::mutex cleanup_mutex_;
    std::condition_variable cleanup_cv_;
    std::thread cleanup_thread_;
    std::atomic<bool> stop_cleanup_{false};
    int cleanup_interval_seconds_ = 300;
};

}  // namespace infrastructure::session
