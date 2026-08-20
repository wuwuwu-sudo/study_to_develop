#pragma once

#include <memory>
#include <string>
#include <vector>
#include "presentation/dto/order_dto.h"
#include "shared/enums.h"
#include "infrastructure/repositories/interfaces/i_order_repository.h"
#include "infrastructure/repositories/interfaces/i_dish_repository.h"
#include "infrastructure/repositories/interfaces/i_user_repository.h"
#include "infrastructure/repositories/interfaces/i_merchant_repository.h"

namespace application {

class OrderService {
public:
    OrderService(
        std::shared_ptr<infrastructure::repositories::IOrderRepository> order_repo,
        std::shared_ptr<infrastructure::repositories::IDishRepository> dish_repo,
        std::shared_ptr<infrastructure::repositories::IUserRepository> user_repo,
        std::shared_ptr<infrastructure::repositories::IMerchantRepository> merchant_repo
    );

    int create_order(int user_id, int merchant_id, const std::vector<OrderItemDto>& items,
                     const std::string& address);
    bool update_order_status(int order_id, int merchant_id, OrderStatus new_status);
    // 顾客确认收货：仅允许“配送中(DELIVERING) → 已完成(DELIVERED)”，并校验顾客归属
    bool complete_order(int order_id, int user_id);
    std::vector<OrderDto> get_orders_for_user(int user_id);
    std::vector<OrderDto> get_orders_for_merchant(int merchant_id);

private:
    std::shared_ptr<infrastructure::repositories::IOrderRepository> order_repo_;
    std::shared_ptr<infrastructure::repositories::IDishRepository> dish_repo_;
    std::shared_ptr<infrastructure::repositories::IUserRepository> user_repo_;
    std::shared_ptr<infrastructure::repositories::IMerchantRepository> merchant_repo_;
};

}  // namespace application
