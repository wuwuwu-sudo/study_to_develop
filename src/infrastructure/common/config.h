#pragma once

#include <string>
#include <unordered_map>

namespace infrastructure::common {

class Config {
public:
    static Config& instance();

    bool load(const std::string& path);
    void use_defaults();

    std::string get_db_path() const;
    int get_db_pool_size() const;
    int get_session_ttl() const;
    // 后台定期清理过期会话的间隔（秒）
    int get_session_cleanup_interval() const;
    int get_int(const std::string& key, int default_value) const;
    std::string get(const std::string& key, const std::string& default_value) const;
    void set(const std::string& key, const std::string& value);

private:
    std::unordered_map<std::string, std::string> values_;
};

}  // namespace infrastructure::common
