#include "user.h"

#include <stdexcept>

User::User() : id_(0), active_(true) {}

User::User(int id, std::string username, std::string password_hash, bool active)
    : id_(id)
    , username_(std::move(username))
    , password_hash_(std::move(password_hash))
    , active_(active) {}

void User::validate() const {
    // 防御性校验：用户名与密码哈希不能为空（对应数据库 NOT NULL 约束）
    if (username_.empty()) {
        throw std::invalid_argument("用户名不能为空");
    }
    if (password_hash_.empty()) {
        throw std::invalid_argument("密码哈希不能为空");
    }
}

int User::get_id() const {
    return id_;
}

const std::string& User::get_username() const {
    return username_;
}

const std::string& User::get_password_hash() const {
    return password_hash_;
}

bool User::is_active() const {
    return active_;
}

void User::set_active(bool active) {
    active_ = active;
}