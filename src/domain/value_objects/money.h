#pragma once
#include <string>
#include <iostream>

class Money {
public:
    Money() = default;
    // 构造时校验：金额必须是有限数值（拒绝 NaN/Inf），否则抛出 std::invalid_argument
    explicit Money(double yuan);

    double get_yuan() const;
    bool is_negative() const;
    // 金额是否为有限有效数值
    bool is_valid() const;
    Money operator+(const Money& other) const;

private:
    double yuan_ = 0.0;
};