#include "application/order_service.h"

#include <optional>
#include <stdexcept>

#include "domain/services/price_calculator.h"
#include "infrastructure/common/exception.h"
#include "infrastructure/common/logger.h"

namespace application {

using infrastructure::common::AppException;
using infrastructure::common::Logger;

namespace {

OrderDto to_dto(const Order& order) {
    OrderDto dto;
    dto.id = order.get_id();
    dto.user_id = order.get_user_id();
    dto.merchant_id = order.get_merchant_id();
    dto.status = order.get_status();
    dto.total = order.get_total().get_yuan();
    dto.address = order.get_address();
    for (const auto& item : order.get_items()) {
        OrderItemDto item_dto;
        item_dto.dish_id = item.get_dish_id();
        item_dto.dish_name = item.get_dish_name();
        item_dto.price = item.get_price().get_yuan();
        item_dto.quantity = item.get_quantity();
        dto.items.push_back(item_dto);
    }
    return dto;
}

}  // namespace

OrderService::OrderService(
    std::shared_ptr<infrastructure::repositories::IOrderRepository> order_repo,
    std::shared_ptr<infrastructure::repositories::IDishRepository> dish_repo,
    std::shared_ptr<infrastructure::repositories::IUserRepository> user_repo,
    std::shared_ptr<infrastructure::repositories::IMerchantRepository> merchant_repo
)
    : order_repo_(std::move(order_repo))
    , dish_repo_(std::move(dish_repo))
    , user_repo_(std::move(user_repo))
    , merchant_repo_(std::move(merchant_repo)) {}

int OrderService::create_order(int user_id, int merchant_id,
                               const std::vector<OrderItemDto>& items,
                               const std::string& address) {
    // 1. 输入验证（防御性编程：先校验再处理）
    if (user_id <= 0) {
        Logger::instance().warn("create_order: invalid user_id=" + std::to_string(user_id));
        throw AppException("用户ID无效");
    }
    if (merchant_id <= 0) {
        Logger::instance().warn("create_order: invalid merchant_id=" + std::to_string(merchant_id));
        throw AppException("商家ID无效");
    }
    if (items.empty()) {
        Logger::instance().warn("create_order: empty items, user=" + std::to_string(user_id));
        throw AppException("订单项不能为空");
    }
    if (address.empty()) {
        Logger::instance().warn("create_order: empty address, user=" + std::to_string(user_id));
        throw AppException("收货地址不能为空");
    }

    // 2. 校验用户与商家存在且有效
    std::optional<User> user;
    try {
        user = user_repo_->find_by_id(user_id);
    } catch (const std::exception& e) {
        Logger::instance().error("create_order user lookup failed: " + std::string(e.what()));
        throw AppException("下单失败，请稍后重试");
    }
    if (!user) {
        Logger::instance().warn("create_order: user not found, id=" + std::to_string(user_id));
        throw AppException("用户不存在");
    }
    if (!user->is_active()) {
        Logger::instance().warn("create_order: inactive user, id=" + std::to_string(user_id));
        throw AppException("账户已被禁用");
    }

    std::optional<Merchant> merchant;
    try {
        merchant = merchant_repo_->find_by_id(merchant_id);
    } catch (const std::exception& e) {
        Logger::instance().error("create_order merchant lookup failed: " + std::string(e.what()));
        throw AppException("下单失败，请稍后重试");
    }
    if (!merchant) {
        Logger::instance().warn("create_order: merchant not found, id=" + std::to_string(merchant_id));
        throw AppException("商家不存在");
    }
    if (!merchant->is_open()) {
        Logger::instance().warn("create_order: merchant not open, id=" + std::to_string(merchant_id));
        throw AppException("商家暂未营业");
    }

    // 3. 校验订单项并从菜品表取真实价格/名称（防御性编程：防止前端篡改价格）
    Order order(user_id, merchant_id);
    order.set_address(address);
    for (const auto& item : items) {
        if (item.dish_id <= 0) {
            Logger::instance().warn("create_order: invalid dish_id=" + std::to_string(item.dish_id));
            throw AppException("菜品ID无效");
        }
        if (item.quantity <= 0) {
            Logger::instance().warn("create_order: invalid quantity=" + std::to_string(item.quantity));
            throw AppException("菜品数量必须大于0");
        }

        std::optional<Dish> dish;
        try {
            dish = dish_repo_->find_by_id(item.dish_id);
        } catch (const std::exception& e) {
            Logger::instance().error("create_order dish lookup failed: " + std::string(e.what()));
            throw AppException("下单失败，请稍后重试");
        }
        if (!dish) {
            Logger::instance().warn("create_order: dish not found, id=" + std::to_string(item.dish_id));
            throw AppException("菜品不存在");
        }
        if (dish->get_merchant_id() != merchant_id) {
            Logger::instance().warn("create_order: dish not owned by merchant, dish=" +
                                    std::to_string(item.dish_id));
            throw AppException("菜品不属于该商家");
        }
        if (!dish->is_available() || dish->is_deleted()) {
            Logger::instance().warn("create_order: dish unavailable, id=" + std::to_string(item.dish_id));
            throw AppException("菜品已下架");
        }

        // 使用服务器端价格与名称，忽略客户端传入的 price/name
        order.add_item(OrderItem(dish->get_id(), dish->get_name(), dish->get_price(), item.quantity));
    }

    // 4. 领域层兜底校验（订单项非空、数量>0）
    try {
        order.validate();
    } catch (const std::invalid_argument& e) {
        Logger::instance().warn("create_order validation: " + std::string(e.what()));
        throw AppException(e.what());
    }

    // 5. 通过价格计算器计算总价（小计 + 折扣 + 配送费）并传入订单聚合根
    try {
        Money calculated = PriceCalculator::calculate_order_total(order, *user, *merchant);
        order.set_total(calculated);
    } catch (const std::exception& e) {
        Logger::instance().error("create_order total calculation failed: " + std::string(e.what()));
        throw AppException("计算订单总价失败，请稍后重试");
    }

    // 6. 保存订单
    int order_id = -1;
    try {
        order_id = order_repo_->save(order);
    } catch (const std::exception& e) {
        Logger::instance().error("create_order save failed: " + std::string(e.what()));
        throw AppException("保存订单失败，请稍后重试");
    }
    if (order_id <= 0) {
        Logger::instance().error("create_order: save returned invalid id");
        throw AppException("保存订单失败，请稍后重试");
    }

    Logger::instance().info("Order created: id=" + std::to_string(order_id) +
                            ", user=" + std::to_string(user_id) +
                            ", merchant=" + std::to_string(merchant_id));
    return order_id;
}

bool OrderService::update_order_status(int order_id, int merchant_id, OrderStatus new_status) {
    // 防御性编程：ID 校验
    if (order_id <= 0) {
        Logger::instance().warn("update_order_status: invalid order_id=" + std::to_string(order_id));
        return false;
    }
    if (merchant_id <= 0) {
        Logger::instance().warn("update_order_status: invalid merchant_id=" + std::to_string(merchant_id));
        return false;
    }

    std::optional<Order> order;
    try {
        order = order_repo_->find_by_id(order_id);
    } catch (const std::exception& e) {
        Logger::instance().error("update_order_status find failed: " + std::string(e.what()));
        return false;
    }
    if (!order) {
        Logger::instance().warn("update_order_status: order not found, id=" + std::to_string(order_id));
        return false;
    }

    // 商家只能操作自己的订单
    if (order->get_merchant_id() != merchant_id) {
        Logger::instance().warn("update_order_status: merchant mismatch, order=" + std::to_string(order_id));
        return false;
    }

    // 订单“完成(DELIVERED)”由顾客确认收货触发，商家不可直接标记完成
    if (new_status == OrderStatus::DELIVERED) {
        Logger::instance().warn("update_order_status: merchant cannot mark as DELIVERED, order=" +
                                std::to_string(order_id));
        return false;
    }

    // 通过状态机校验并改变订单状态（非法转换抛出 std::invalid_argument）
    try {
        order->transition_to(new_status);
    } catch (const std::invalid_argument& e) {
        Logger::instance().warn("update_order_status: illegal transition, order=" +
                                std::to_string(order_id) + ": " + e.what());
        return false;
    }

    bool ok = false;
    try {
        ok = order_repo_->update(*order);
    } catch (const std::exception& e) {
        Logger::instance().error("update_order_status update failed: " + std::string(e.what()));
        return false;
    }
    if (ok) {
        Logger::instance().info("Order status updated: id=" + std::to_string(order_id));
    }
    return ok;
}

bool OrderService::complete_order(int order_id, int user_id) {
    // 防御性编程：ID 校验
    if (order_id <= 0) {
        Logger::instance().warn("complete_order: invalid order_id=" + std::to_string(order_id));
        return false;
    }
    if (user_id <= 0) {
        Logger::instance().warn("complete_order: invalid user_id=" + std::to_string(user_id));
        return false;
    }

    std::optional<Order> order;
    try {
        order = order_repo_->find_by_id(order_id);
    } catch (const std::exception& e) {
        Logger::instance().error("complete_order find failed: " + std::string(e.what()));
        return false;
    }
    if (!order) {
        Logger::instance().warn("complete_order: order not found, id=" + std::to_string(order_id));
        return false;
    }

    // 顾客只能确认自己的订单
    if (order->get_user_id() != user_id) {
        Logger::instance().warn("complete_order: user mismatch, order=" +
                                std::to_string(order_id));
        return false;
    }

    // 状态机校验：仅“配送中 → 已完成”为合法转换，其余（含终态/非法状态）由状态机拒绝
    try {
        order->transition_to(OrderStatus::DELIVERED);
    } catch (const std::invalid_argument& e) {
        Logger::instance().warn("complete_order: illegal transition, order=" +
                                std::to_string(order_id) + ": " + e.what());
        return false;
    }

    bool ok = false;
    try {
        ok = order_repo_->update(*order);
    } catch (const std::exception& e) {
        Logger::instance().error("complete_order update failed: " + std::string(e.what()));
        return false;
    }
    if (ok) {
        Logger::instance().info("Order completed by customer: id=" + std::to_string(order_id));
    }
    return ok;
}

std::vector<OrderDto> OrderService::get_orders_for_user(int user_id) {
    std::vector<OrderDto> result;
    // 防御性编程：ID 非法时直接返回空列表
    if (user_id <= 0) {
        Logger::instance().warn("get_orders_for_user: invalid user_id=" + std::to_string(user_id));
        return result;
    }
    try {
        for (const auto& order : order_repo_->find_by_user(user_id)) {
            result.push_back(to_dto(order));
        }
    } catch (const std::exception& e) {
        Logger::instance().error("get_orders_for_user failed: " + std::string(e.what()));
        throw AppException("查询订单失败，请稍后重试");
    }
    return result;
}

std::vector<OrderDto> OrderService::get_orders_for_merchant(int merchant_id) {
    std::vector<OrderDto> result;
    // 防御性编程：ID 非法时直接返回空列表
    if (merchant_id <= 0) {
        Logger::instance().warn("get_orders_for_merchant: invalid merchant_id=" +
                                std::to_string(merchant_id));
        return result;
    }
    try {
        for (const auto& order : order_repo_->find_by_merchant(merchant_id)) {
            result.push_back(to_dto(order));
        }
    } catch (const std::exception& e) {
        Logger::instance().error("get_orders_for_merchant failed: " + std::string(e.what()));
        throw AppException("查询订单失败，请稍后重试");
    }
    return result;
}

}  // namespace application
