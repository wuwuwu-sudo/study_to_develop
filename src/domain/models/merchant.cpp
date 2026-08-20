#include "merchant.h"

#include <stdexcept>

Merchant::Merchant() : id_(0), is_open_(true) {}

Merchant::Merchant(int id, std::string username, std::string shop_name, std::string address, bool is_open)
    : id_(id)
    , username_(std::move(username))
    , shop_name_(std::move(shop_name))
    , address_(std::move(address))
    , is_open_(is_open) {}

void Merchant::validate() const {
    // 防御性校验：用户名、店铺名称、地址均不能为空（对应数据库 NOT NULL 约束）
    if (username_.empty()) {
        throw std::invalid_argument("用户名不能为空");
    }
    if (shop_name_.empty()) {
        throw std::invalid_argument("店铺名称不能为空");
    }
    if (address_.empty()) {
        throw std::invalid_argument("店铺地址不能为空");
    }
}

int Merchant::get_id() const {
    return id_;
}

const std::string& Merchant::get_username() const {
    return username_;
}

const std::string& Merchant::get_password_hash() const {
    return password_hash_;
}

const std::string& Merchant::get_shop_name() const {
    return shop_name_;
}

const std::string& Merchant::get_address() const {
    return address_;
}

bool Merchant::is_open() const {
    return is_open_;
}

void Merchant::set_open(bool open) {
    is_open_ = open;
}

void Merchant::set_password_hash(const std::string& hash) {
    password_hash_ = hash;
}