#pragma once
#include <string>
#include <iostream>

class User {
public:
    User();
    User(int id, std::string username, std::string password_hash, bool active = true);

    // 校验用户合法性（用户名/密码哈希非空），非法时抛出 std::invalid_argument
    void validate() const;

    int get_id() const;
    const std::string& get_username() const;
    const std::string& get_password_hash() const;
    bool is_active() const;
    void set_active(bool active);

private:
    int id_ = 0;
    std::string username_;
    std::string password_hash_;
    bool active_ = true;
};