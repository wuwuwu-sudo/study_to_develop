#include "money.h"

#include <cmath>
#include <stdexcept>

namespace {

// 防御性工具：校验金额必须为有限数值，否则抛出 std::invalid_argument
void validate_finite(double value, const char* what) {
    if (!std::isfinite(value)) {
        throw std::invalid_argument(std::string(what) + "必须是有限数值");
    }
}

}  // namespace

Money::Money(double yuan) : yuan_(yuan) {
    // 防御性：拒绝 NaN/Inf 金额，防止污染后续计算
    validate_finite(yuan_, "金额");
}

double Money::get_yuan() const {
    return yuan_;
}

bool Money::is_negative() const {
    return yuan_ < 0;
}

bool Money::is_valid() const {
    return std::isfinite(yuan_);
}

Money Money::operator+(const Money& other) const {
    // 加法结果由 Money 构造器做有限性校验
    return Money(yuan_ + other.yuan_);
}