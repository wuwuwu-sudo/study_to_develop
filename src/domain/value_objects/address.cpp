#include "address.h"

#include <cstddef>
#include <stdexcept>
#include <vector>

Address::Address(std::string province, std::string city, std::string detail)
    : province_(std::move(province))
    , city_(std::move(city))
    , detail_(std::move(detail)) {}

void Address::validate() const {
    // 防御性校验：省份、城市、详细地址均不能为空
    if (province_.empty()) {
        throw std::invalid_argument("省份不能为空");
    }
    if (city_.empty()) {
        throw std::invalid_argument("城市不能为空");
    }
    if (detail_.empty()) {
        throw std::invalid_argument("详细地址不能为空");
    }
}

std::string Address::full_text() const {
    // 防御性：仅拼接非空部分，用单个空格分隔，避免前导/尾随/连续多余空格
    std::vector<std::string> parts;
    if (!province_.empty()) {
        parts.push_back(province_);
    }
    if (!city_.empty()) {
        parts.push_back(city_);
    }
    if (!detail_.empty()) {
        parts.push_back(detail_);
    }

    std::string result;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) {
            result += ' ';
        }
        result += parts[i];
    }
    return result;
}