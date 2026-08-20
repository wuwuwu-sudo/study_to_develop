#pragma once

#include <optional>
#include <string>
#include <vector>
#include "domain/models/merchant.h"

namespace infrastructure::repositories {

class IMerchantRepository {
public:
    virtual ~IMerchantRepository() = default;

    virtual std::optional<Merchant> find_by_id(int merchant_id) = 0;
    virtual std::optional<Merchant> find_by_username(const std::string& username) = 0;
    virtual int save(const Merchant& merchant) = 0;
    // 查询所有营业中的商家（用于 /api/shops）
    virtual std::vector<Merchant> find_open_merchants() = 0;
};

}  // namespace infrastructure::repositories
