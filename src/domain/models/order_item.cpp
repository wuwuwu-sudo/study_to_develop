#include "order_item.h"

#include <stdexcept>

OrderItem::OrderItem() : dish_id_(0), quantity_(0) {}

OrderItem::OrderItem(int dish_id, std::string dish_name, Money price, int quantity)
    : dish_id_(dish_id)
    , dish_name_(std::move(dish_name))
    , price_(price)
    , quantity_(quantity) {}

void OrderItem::validate() const {
    // 防御性校验：dish_id 与数量必须大于0、价格非负
    if (dish_id_ <= 0) {
        throw std::invalid_argument("菜品ID必须大于0");
    }
    if (quantity_ <= 0) {
        throw std::invalid_argument("数量必须大于0");
    }
    if (price_.is_negative()) {
        throw std::invalid_argument("价格不能为负数");
    }
}

int OrderItem::get_dish_id() const {
    return dish_id_;
}

const std::string& OrderItem::get_dish_name() const {
    return dish_name_;
}

Money OrderItem::get_price() const {
    return price_;
}

int OrderItem::get_quantity() const {
    return quantity_;
}

Money OrderItem::get_subtotal() const {
    return Money(price_.get_yuan() * quantity_);
}