#pragma once
#include <vector>
#include <string>
#include <iostream>
#include "domain/value_objects/money.h"
#include "domain/value_objects/order_status.h"
#include "order_item.h"

class Order {
public:
    Order();
    Order(int user_id, int merchant_id);
    Order(int id, int user_id, int merchant_id);

    void add_item(const OrderItem& item);
    void validate() const;
    // 校验状态转换合法性并变更状态；非法转换抛出 std::invalid_argument
    void transition_to(OrderStatus new_status);
    void confirm();
    void start_delivery();
    void complete_delivery();
    void cancel();

    int get_id() const;
    int get_user_id() const;
    int get_merchant_id() const;
    OrderStatus get_status() const;
    Money get_total() const;
    const std::vector<OrderItem>& get_items() const;

    // 设置由外部（价格计算器）计算好的订单总价；负数值抛出 std::invalid_argument
    void set_total(const Money& total);

    // 从持久化存储恢复订单状态时使用（绕过状态机校验）
    void set_status(OrderStatus status);

    // 收货地址（外卖配送地址，创建订单时必填）
    std::string get_address() const;
    void set_address(const std::string& address);

private:
    int id_ = 0;
    int user_id_ = 0;
    int merchant_id_ = 0;
    OrderStatus status_ = OrderStatus::PENDING;
    Money total_;
    std::string address_;
    std::vector<OrderItem> items_;
};