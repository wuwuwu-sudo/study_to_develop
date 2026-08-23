#pragma once
#include <functional>
#include <string>
#include <vector>
#include <iostream>
#include "domain/value_objects/money.h"
#include "shared/enums.h"
// 依赖: domain/value objects (Money), shared/enums (OrderStatus)

class Dish {
public:
    Dish();
    Dish(int merchant_id, std::string name, Money price, std::string category, std::string description);
    Dish(int id, int merchant_id, std::string name, Money price, std::string category, std::string description);

    // 校验菜品合法性（名称非空、价格非负），非法时抛出 std::invalid_argument
    void validate() const;
    void set_available(bool available);
    void soft_delete();
    // 是否可删除：未删除且不存在活跃订单
    bool is_deletable() const;
    // 是否存在活跃订单（由外部注入回调查询；未注入时视为无，即允许删除）
    bool has_active_orders() const;
    // 注入“是否存在活跃订单”的检查回调（防御性编程：领域模型与仓储解耦）
    void set_active_orders_check(std::function<bool()> check);

    int get_id() const;
    int get_merchant_id() const;
    const std::string& get_name() const;
    Money get_price() const;
    const std::string& get_category() const;
    const std::string& get_description() const;
    bool is_available() const;
    bool is_deleted() const;
    // 乐观锁版本号（持久化；由仓储从 DB 恢复，写时做 version 条件更新）
    int get_version() const;
    void set_version(int version);

private:
    int id_ = 0;
    int merchant_id_ = 0;
    std::string name_;
    Money price_;
    std::string category_;
    std::string description_;
    bool available_ = true;
    bool deleted_ = false;
    int version_ = 0;
    std::function<bool()> active_orders_check_;
};