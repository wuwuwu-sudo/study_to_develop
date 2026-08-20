#pragma once
#include <string>
#include <iostream>

class Merchant {
public:
    Merchant();
    Merchant(int id, std::string username, std::string shop_name, std::string address, bool is_open = true);

    // 校验商家合法性（用户名/店名/地址非空），非法时抛出 std::invalid_argument
    void validate() const;

    int get_id() const;
    const std::string& get_username() const;
    const std::string& get_password_hash() const;
    const std::string& get_shop_name() const;
    const std::string& get_address() const;
    bool is_open() const;
    void set_open(bool open);
    // 设置密码哈希（商家密码认证用）
    void set_password_hash(const std::string& hash);

private:
    int id_ = 0;
    std::string username_;
    std::string password_hash_;
    std::string shop_name_;
    std::string address_;
    bool is_open_ = true;
};