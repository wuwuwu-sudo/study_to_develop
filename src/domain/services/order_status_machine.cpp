#include "order_status_machine.h"

namespace {

// 防御性：校验状态值是否为合法枚举成员（防止通过 static_cast 混入非法值）
bool is_valid_status(OrderStatus status) {
    switch (status) {
        case OrderStatus::PENDING:
        case OrderStatus::CONFIRMED:
        case OrderStatus::DELIVERING:
        case OrderStatus::DELIVERED:
        case OrderStatus::CANCELLED:
            return true;
        default:
            return false;
    }
}

}  // namespace

bool OrderStatusMachine::can_transition(OrderStatus from, OrderStatus to) {
    // 防御性：任一状态非法时一律拒绝（fail-closed），绝不放行未知状态
    if (!is_valid_status(from) || !is_valid_status(to)) {
        return false;
    }

    // 防御性：状态不允许原地自转（自环转换恒为非法）
    if (from == to) {
        return false;
    }

    // 状态转换规则：
    //   PENDING    → CONFIRMED / CANCELLED
    //   CONFIRMED  → DELIVERING / CANCELLED
    //   DELIVERING → DELIVERED / CANCELLED
    //   DELIVERED / CANCELLED 为终态，不可再转换
    switch (from) {
        case OrderStatus::PENDING:
            return to == OrderStatus::CONFIRMED || to == OrderStatus::CANCELLED;

        case OrderStatus::CONFIRMED:
            return to == OrderStatus::DELIVERING || to == OrderStatus::CANCELLED;

        case OrderStatus::DELIVERING:
            return to == OrderStatus::DELIVERED || to == OrderStatus::CANCELLED;

        case OrderStatus::DELIVERED:
        case OrderStatus::CANCELLED:
            return false;  // 终态，不可再转换

        default:
            // 防御性兜底：理论上已被 is_valid_status 拦截，绝不放行
            return false;
    }
}