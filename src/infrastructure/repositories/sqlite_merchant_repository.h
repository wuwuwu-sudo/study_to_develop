#pragma once

#include <optional>
#include <string>
#include <vector>
#include "infrastructure/repositories/interfaces/i_merchant_repository.h"
#include "infrastructure/database/db_manager.h"

namespace infrastructure::repositories {

class SqliteMerchantRepository : public IMerchantRepository {
public:
    explicit SqliteMerchantRepository(infrastructure::database::DbManager& db);

    std::optional<Merchant> find_by_id(int merchant_id) override;
    std::optional<Merchant> find_by_username(const std::string& username) override;
    int save(const Merchant& merchant) override;
    bool update_open_status(int merchant_id, bool open) override;
    std::vector<Merchant> find_open_merchants() override;

private:
    infrastructure::database::DbManager& db_;
};

}  // namespace infrastructure::repositories
