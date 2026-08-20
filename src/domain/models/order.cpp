#include "order.h"

#include <stdexcept>

#include "domain/services/order_status_machine.h"

Order::Order() : id_(0), user_id_(0), merchant_id_(0), status_(OrderStatus::PENDING) {}

Order::Order(int user_id, int merchant_id)
    : id_(0)
    , user_id_(user_id)
    , merchant_id_(merchant_id)
    , status_(OrderStatus::PENDING) {}

Order::Order(int id, int user_id, int merchant_id)
    : id_(id)
    , user_id_(user_id)
    , merchant_id_(merchant_id)
    , status_(OrderStatus::PENDING) {}

void Order::add_item(const OrderItem& item) {
    items_.push_back(item);
    // 重新计算总价
    double total = 0.0;
    for (const auto& i : items_) {
        total += i.get_subtotal().get_yuan();
    }
    total_ = Money(total);
}

void Order::validate() const {
    // 防御性校验：订单项不能为空、所有订单项数量必须大于0、收货地址不能为空
    if (items_.empty()) {
        throw std::invalid_argument("订单项不能为空");
    }
    for (const auto& item : items_) {
        if (item.get_quantity() <= 0) {
            throw std::invalid_argument("订单项数量必须大于0");
        }
    }
    if (address_.empty()) {
        throw std::invalid_argument("收货地址不能为空");
    }
}

void Order::transition_to(OrderStatus new_status) {
    // 防御性编程：非法状态转换直接抛出异常（不再静默忽略）
    if (!OrderStatusMachine::can_transition(status_, new_status)) {
        throw std::invalid_argument("非法订单状态转换");
    }
    status_ = new_status;
}

void Order::confirm() {
    transition_to(OrderStatus::CONFIRMED);
}

void Order::start_delivery() {
    transition_to(OrderStatus::DELIVERING);
}

void Order::complete_delivery() {
    transition_to(OrderStatus::DELIVERED);
}

void Order::cancel() {
    transition_to(OrderStatus::CANCELLED);
}

int Order::get_id() const {
    return id_;
}

int Order::get_user_id() const {
    return user_id_;
}

int Order::get_merchant_id() const {
    return merchant_id_;
}

OrderStatus Order::get_status() const {
    return status_;
}

Money Order::get_total() const {
    return total_;
}

const std::vector<OrderItem>& Order::get_items() const {
    return items_;
}

void Order::set_total(const Money& total) {
    // 防御性：订单总价不能为负数
    if (total.is_negative()) {
        throw std::invalid_argument("订单总价不能为负数");
    }
    total_ = total;
}

void Order::set_status(OrderStatus status) {
    status_ = status;
}

std::string Order::get_address() const {
    return address_;
}

void Order::set_address(const std::string& address) {
    address_ = address;
}
