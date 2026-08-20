#pragma once
#include <iostream>
#include "domain/value_objects/order_status.h"

class OrderStatusMachine {
public:
    // 防御性契约：传入非法状态（越界枚举）时一律返回 false（fail-closed）；
    // 自环转换（from == to）恒为非法。
    static bool can_transition(OrderStatus from, OrderStatus to);
};