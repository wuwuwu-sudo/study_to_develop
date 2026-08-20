#pragma once

#include <chrono>
#include <cstddef>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace infrastructure::session {

struct SessionInfo {
    std::string session_id;
    int user_id = 0;
    int merchant_id = 0;
    std::chrono::system_clock::time_point created_at;
    std::chrono::system_clock::time_point expire_time;
};

class SessionStore {
public:
    virtual ~SessionStore() = default;

    virtual bool save(const SessionInfo& session) = 0;
    virtual std::optional<SessionInfo> find(const std::string& session_id) = 0;
    virtual bool remove(const std::string& session_id) = 0;

    // 获取当前存储中的所有会话（含尚未被访问清除的过期会话）。
    // 供批量清理、统计等场景使用。
    virtual std::vector<SessionInfo> all_sessions() = 0;
};

class InMemorySessionStore : public SessionStore {
public:
    bool save(const SessionInfo& session) override;
    std::optional<SessionInfo> find(const std::string& session_id) override;
    bool remove(const std::string& session_id) override;
    std::vector<SessionInfo> all_sessions() override;

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, SessionInfo> sessions_;
};

}  // namespace infrastructure::session
