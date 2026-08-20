#include "infrastructure/session/session_store.h"

#include <algorithm>
#include <chrono>

namespace infrastructure::session {

bool InMemorySessionStore::save(const SessionInfo& session) {
    std::lock_guard<std::mutex> lock(mutex_);
    sessions_[session.session_id] = session;
    return true;
}

std::optional<SessionInfo> InMemorySessionStore::find(const std::string& session_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(session_id);
    if (it == sessions_.end()) {
        return std::nullopt;
    }
    if (std::chrono::system_clock::now() > it->second.expire_time) {
        sessions_.erase(it);
        return std::nullopt;
    }
    return it->second;
}

bool InMemorySessionStore::remove(const std::string& session_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    return sessions_.erase(session_id) > 0;
}

std::vector<SessionInfo> InMemorySessionStore::all_sessions() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<SessionInfo> result;
    result.reserve(sessions_.size());
    for (const auto& [id, info] : sessions_) {
        (void)id;
        result.push_back(info);
    }
    return result;
}

}  // namespace infrastructure::session
