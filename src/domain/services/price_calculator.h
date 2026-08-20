#pragma once
#include <vector>
#include <iostream>
#include "domain/value_objects/money.h"
#include "domain/models/order.h"
#include "domain/models/user.h"
#include "domain/models/merchant.h"

class PriceCalculator {
public:
    static Money calculate_order_total(const Order& order, const User& user, const Merchant& merchant);
    static Money calculate_subtotal(const std::vector<OrderItem>& items);
    static Money apply_discounts(Money total, const User& user, const Merchant& merchant);
    static Money calculate_delivery_fee(Money total, const User& user);
};