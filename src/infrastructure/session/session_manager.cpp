#include "infrastructure/session/session_manager.h"

#include <chrono>
#include <random>

#include "infrastructure/common/logger.h"

namespace infrastructure::session {

SessionManager& SessionManager::instance() {
    static SessionManager manager;
    return manager;
}

SessionManager::SessionManager()
    : store_(std::make_shared<InMemorySessionStore>()) {}

SessionManager::~SessionManager() {
    stop_cleanup_loop();
}

void SessionManager::initialize(int ttl_seconds) {
    if (ttl_seconds > 0) {
        ttl_seconds_ = ttl_seconds;
    }
}

void SessionManager::set_store(std::shared_ptr<SessionStore> store) {
    if (store) {
        store_ = std::move(store);
    }
}

std::size_t SessionManager::cleanup_expired() {
    const auto now = std::chrono::system_clock::now();
    std::vector<SessionInfo> sessions = store_->all_sessions();

    std::size_t removed = 0;
    for (const auto& session : sessions) {
        if (session.expire_time <= now) {
            if (store_->remove(session.session_id)) {
                ++removed;
            }
        }
    }
    return removed;
}

void SessionManager::start_cleanup_loop(int interval_seconds) {
    std::lock_guard<std::mutex> lock(cleanup_mutex_);
    if (cleanup_thread_.joinable()) {
        // 已在运行，避免重复启动
        return;
    }
    if (interval_seconds <= 0) {
        interval_seconds = 1;
    }
    cleanup_interval_seconds_ = interval_seconds;
    stop_cleanup_.store(false);

    cleanup_thread_ = std::thread([this]() {
        std::unique_lock<std::mutex> lock(cleanup_mutex_);
        while (true) {
            if (cleanup_cv_.wait_for(
                    lock,
                    std::chrono::seconds(cleanup_interval_seconds_),
                    [this]() { return stop_cleanup_.load(); })) {
                // 收到停止通知
                break;
            }
            // 定时到期，批量清理过期会话
            const std::size_t removed = cleanup_expired();
            if (removed > 0) {
                LOG_INFO("Session cleanup: removed " +
                         std::to_string(removed) + " expired session(s)");
            }
        }
    });
}

void SessionManager::stop_cleanup_loop() {
    {
        std::lock_guard<std::mutex> lock(cleanup_mutex_);
        stop_cleanup_.store(true);
    }
    cleanup_cv_.notify_all();
    if (cleanup_thread_.joinable()) {
        cleanup_thread_.join();
    }
}

std::size_t SessionManager::session_count() const {
    return store_->all_sessions().size();
}

std::string SessionManager::create_session(int user_id, int merchant_id) {
    std::string session_id = generate_hex_id(32);
    SessionInfo info;
    info.session_id = session_id;
    info.user_id = user_id;
    info.merchant_id = merchant_id;
    info.created_at = std::chrono::system_clock::now();
    info.expire_time = info.created_at + std::chrono::seconds(ttl_seconds_);
    store_->save(info);
    return session_id;
}

bool SessionManager::validate_session(const std::string& session_id) {
    return store_->find(session_id).has_value();
}

std::optional<SessionInfo> SessionManager::find(const std::string& session_id) {
    // 防御性：空会话或存储未初始化时返回 nullopt
    if (session_id.empty() || !store_) {
        return std::nullopt;
    }
    return store_->find(session_id);
}

void SessionManager::destroy_session(const std::string& session_id) {
    store_->remove(session_id);
}

std::string SessionManager::generate_hex_id(std::size_t length) {
    static const char* hex_chars = "0123456789abcdef";
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, 15);

    std::string result;
    result.reserve(length);
    for (std::size_t i = 0; i < length; ++i) {
        result += hex_chars[dis(gen)];
    }
    return result;
}

}  // namespace infrastructure::session
