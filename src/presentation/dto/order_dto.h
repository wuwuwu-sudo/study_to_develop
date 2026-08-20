#pragma once
#include <string>
#include <vector>
#include <iostream>
#include "shared/enums.h"

struct OrderItemDto {
    int dish_id = 0;
    std::string dish_name;
    double price = 0.0;
    int quantity = 0;
};

struct OrderDto {
    int id = 0;
    int user_id = 0;
    int merchant_id = 0;
    OrderStatus status = OrderStatus::PENDING;
    double total = 0.0;
    std::string address;
    std::string remark;
    std::string created_at;
    std::vector<OrderItemDto> items;
};