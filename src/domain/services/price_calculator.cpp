#include "price_calculator.h"

#include <cmath>
#include <stdexcept>
#include <string>

namespace {

// 防御性工具：校验金额必须为有限且非负的数值，否则抛出 std::invalid_argument
void validate_money_value(double value, const char* what) {
    if (!std::isfinite(value)) {
        throw std::invalid_argument(std::string(what) + "必须是有限数值");
    }
    if (value < 0.0) {
        throw std::invalid_argument(std::string(what) + "不能为负数");
    }
}

}  // namespace

Money PriceCalculator::calculate_order_total(const Order& order, const User& user, const Merchant& merchant) {
    // 防御性：入口处先校验用户与商家数据合法性（fail-fast）
    user.validate();
    merchant.validate();

    // 防御性：允许空订单（无订单项时仅计算配送费）；订单项本身的合法性由小计函数逐项校验
    Money subtotal = calculate_subtotal(order.get_items());
    Money discounted = apply_discounts(subtotal, user, merchant);
    Money delivery_fee = calculate_delivery_fee(discounted, user);

    // 防御性：最终总价必须为有限且非负的数值
    double total = discounted.get_yuan() + delivery_fee.get_yuan();
    validate_money_value(total, "订单总价");
    return Money(total);
}

Money PriceCalculator::calculate_subtotal(const std::vector<OrderItem>& items) {
    // 防御性：空订单项返回 0 元
    if (items.empty()) {
        return Money(0.0);
    }

    double total = 0.0;
    for (const auto& item : items) {
        // 防御性：逐个校验订单项（dish_id/数量>0、价格非负），非法项直接抛出异常
        item.validate();

        double subtotal = item.get_subtotal().get_yuan();
        // 防御性：防止 NaN/Inf 污染累加结果
        validate_money_value(subtotal, "订单项小计");
        total += subtotal;
    }

    // 防御性：累加结果必须为有限且非负的数值
    validate_money_value(total, "小计");
    return Money(total);
}

Money PriceCalculator::apply_discounts(Money total, const User& user, const Merchant& merchant) {
    // 防御性：金额必须为有限且非负的数值
    validate_money_value(total.get_yuan(), "折扣金额");
    // 防御性：校验用户与商家数据合法性（为后续折扣规则提供可靠输入）
    user.validate();
    merchant.validate();

    // TODO: 实现折扣逻辑
    // - 会员折扣
    // - 满减优惠
    // - 商家优惠

    // 占位实现：暂无折扣规则，原样返回
    return total;
}

Money PriceCalculator::calculate_delivery_fee(Money total, const User& user) {
    // 防御性：金额必须为有限且非负的数值
    validate_money_value(total.get_yuan(), "配送费计算金额");
    // 防御性：校验用户数据合法性
    user.validate();

    // TODO: 实现配送费计算
    // - 满额免配送费
    // - 按距离计算
    // - 特殊时段加价

    return Money(5.0);  // 默认5元配送费
}