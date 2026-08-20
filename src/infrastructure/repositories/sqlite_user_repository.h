#pragma once

#include <optional>
#include <string>
#include "infrastructure/repositories/interfaces/i_user_repository.h"
#include "infrastructure/database/db_manager.h"

namespace infrastructure::repositories {

class SqliteUserRepository : public IUserRepository {
public:
    explicit SqliteUserRepository(infrastructure::database::DbManager& db);

    std::optional<User> find_by_id(int user_id) override;
    std::optional<User> find_by_username(const std::string& username) override;
    int save(const User& user) override;

private:
    infrastructure::database::DbManager& db_;
};

}  // namespace infrastructure::repositories
