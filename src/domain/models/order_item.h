#pragma once
#include <string>
#include <iostream>
#include "domain/value_objects/money.h"

class OrderItem {
public:
    OrderItem();
    OrderItem(int dish_id, std::string dish_name, Money price, int quantity);

    // 校验订单项合法性（dish_id/数量必须大于0、价格非负），非法时抛出 std::invalid_argument
    void validate() const;

    int get_dish_id() const;
    const std::string& get_dish_name() const;
    Money get_price() const;
    int get_quantity() const;
    Money get_subtotal() const;

private:
    int dish_id_ = 0;
    std::string dish_name_;
    Money price_;
    int quantity_ = 0;
};