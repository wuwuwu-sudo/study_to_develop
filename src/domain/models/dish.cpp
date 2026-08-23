#include "dish.h"

#include <stdexcept>
#include <utility>

Dish::Dish() : id_(0), merchant_id_(0), available_(true), deleted_(false) {}

Dish::Dish(int merchant_id, std::string name, Money price, std::string category, std::string description)
    : id_(0)
    , merchant_id_(merchant_id)
    , name_(std::move(name))
    , price_(price)
    , category_(std::move(category))
    , description_(std::move(description))
    , available_(true)
    , deleted_(false) {}

Dish::Dish(int id, int merchant_id, std::string name, Money price, std::string category, std::string description)
    : id_(id)
    , merchant_id_(merchant_id)
    , name_(std::move(name))
    , price_(price)
    , category_(std::move(category))
    , description_(std::move(description))
    , available_(true)
    , deleted_(false) {}

void Dish::validate() const {
    // 防御性校验：名称非空、价格非负
    // （分类允许为空：前端添加菜品时分类可为空提交，数据库也允许空字符串）
    if (name_.empty()) {
        throw std::invalid_argument("菜品名称不能为空");
    }
    if (price_.is_negative()) {
        throw std::invalid_argument("菜品价格不能为负数");
    }
}

void Dish::set_available(bool available) {
    available_ = available;
}

void Dish::soft_delete() {
    deleted_ = true;
}

void Dish::set_active_orders_check(std::function<bool()> check) {
    active_orders_check_ = std::move(check);
}

bool Dish::has_active_orders() const {
    // 防御性编程：未注入检查回调时，默认视为无活跃订单（允许删除）。
    // 需要真实判断的调用方（如删除流程）必须通过 set_active_orders_check 注入查询。
    if (!active_orders_check_) {
        return false;
    }
    return active_orders_check_();
}

bool Dish::is_deletable() const {
    // 已删除或存在活跃订单时不可删除
    if (deleted_) {
        return false;
    }
    return !has_active_orders();
}

int Dish::get_id() const {
    return id_;
}

int Dish::get_merchant_id() const {
    return merchant_id_;
}

const std::string& Dish::get_name() const {
    return name_;
}

Money Dish::get_price() const {
    return price_;
}

const std::string& Dish::get_category() const {
    return category_;
}

const std::string& Dish::get_description() const {
    return description_;
}

bool Dish::is_available() const {
    return available_;
}

bool Dish::is_deleted() const {
    return deleted_;
}

int Dish::get_version() const {
    return version_;
}

void Dish::set_version(int version) {
    version_ = version;
}