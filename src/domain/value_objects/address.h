#pragma once
#include <string>
#include <iostream>

class Address {
public:
    Address() = default;
    Address(std::string province, std::string city, std::string detail);

    // 校验地址合法性（省/市/详细地址非空），非法时抛出 std::invalid_argument
    void validate() const;

    // 拼接完整地址文本（跳过空部分，避免多余空格）
    std::string full_text() const;

private:
    std::string province_;
    std::string city_;
    std::string detail_;
};