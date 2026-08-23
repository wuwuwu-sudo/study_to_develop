#include "infrastructure/common/config.h"

#include <fstream>
#include <sstream>

#include "infrastructure/common/exception.h"
#include "shared/constants.h"

//解析配置文件
namespace infrastructure::common {

Config& Config::instance() {
    static Config instance;
    return instance;
}

bool Config::load(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        throw ConfigLoadException("cannot open config file: " + path);
    }

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') {
            continue;
        }

        std::size_t pos = line.find('=');
        if (pos == std::string::npos) {
            continue;
        }

        std::string key = line.substr(0, pos);
        std::string value = line.substr(pos + 1);
        auto trim = [](std::string& text) {
            text.erase(0, text.find_first_not_of(" \t\r\n"));
            text.erase(text.find_last_not_of(" \t\r\n") + 1);
        };
        trim(key);
        trim(value);
        values_[key] = value;
    }
    return true;
}

void Config::use_defaults() {
    if (values_.find("db.path") == values_.end()) {
        values_["db.path"] = "food_delivery.db";
    }
    if (values_.find("db.pool_size") == values_.end()) {
        values_["db.pool_size"] = std::to_string(constants::DEFAULT_DB_POOL_SIZE);
    }
    if (values_.find("db.worker_threads") == values_.end()) {
        values_["db.worker_threads"] =
            std::to_string(constants::DEFAULT_DB_WORKER_THREADS);
    }
    if (values_.find("session.ttl") == values_.end()) {
        values_["session.ttl"] = std::to_string(constants::DEFAULT_SESSION_TTL_SECONDS);
    }
    if (values_.find("session.cleanup_interval") == values_.end()) {
        values_["session.cleanup_interval"] =
            std::to_string(constants::DEFAULT_SESSION_CLEANUP_INTERVAL_SECONDS);
    }
    if (values_.find("server.task_queue") == values_.end()) {
        values_["server.task_queue"] =
            std::to_string(constants::DEFAULT_TASK_QUEUE_ENABLED);
    }
    if (values_.find("server.task_queue_consumers") == values_.end()) {
        values_["server.task_queue_consumers"] =
            std::to_string(constants::DEFAULT_TASK_QUEUE_CONSUMERS);
    }
    if (values_.find("cache.local.ttl") == values_.end()) {
        values_["cache.local.ttl"] = std::to_string(constants::DEFAULT_CACHE_TTL_SECONDS);
    }
    if (values_.find("cache.local.max_entries") == values_.end()) {
        values_["cache.local.max_entries"] =
            std::to_string(constants::DEFAULT_CACHE_LOCAL_MAX_ENTRIES);
    }
    if (values_.find("cache.redis.enabled") == values_.end()) {
        values_["cache.redis.enabled"] = "1";
    }
    if (values_.find("cache.redis.host") == values_.end()) {
        values_["cache.redis.host"] = "127.0.0.1";
    }
    if (values_.find("cache.redis.port") == values_.end()) {
        values_["cache.redis.port"] = "6379";
    }
    if (values_.find("cache.redis.ttl") == values_.end()) {
        values_["cache.redis.ttl"] = std::to_string(constants::DEFAULT_CACHE_REDIS_TTL_SECONDS);
    }
    if (values_.find("cache.redis.timeout_ms") == values_.end()) {
        values_["cache.redis.timeout_ms"] = "300";
    }
    if (values_.find("cache.redis.worker_threads") == values_.end()) {
        values_["cache.redis.worker_threads"] =
            std::to_string(constants::DEFAULT_REDIS_WORKER_THREADS);
    }
}

std::string Config::get_db_path() const {
    return get("db.path", "food_delivery.db");
}

int Config::get_db_pool_size() const {
    return get_int("db.pool_size", constants::DEFAULT_DB_POOL_SIZE);
}

int Config::get_session_ttl() const {
    return get_int("session.ttl", constants::DEFAULT_SESSION_TTL_SECONDS);
}

int Config::get_session_cleanup_interval() const {
    return get_int("session.cleanup_interval",
                   constants::DEFAULT_SESSION_CLEANUP_INTERVAL_SECONDS);
}

int Config::get_int(const std::string& key, int default_value) const {
    auto it = values_.find(key);
    if (it == values_.end()) {
        return default_value;
    }
    try {
        return std::stoi(it->second);
    } catch (const std::exception&) {
        return default_value;
    }
}

std::string Config::get(const std::string& key, const std::string& default_value) const {
    auto it = values_.find(key);
    return it == values_.end() ? default_value : it->second;
}

void Config::set(const std::string& key, const std::string& value) {
    values_[key] = value;
}

}  // namespace infrastructure::common
