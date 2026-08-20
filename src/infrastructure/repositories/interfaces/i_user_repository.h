#pragma once

#include <optional>
#include <string>
#include "domain/models/user.h"

namespace infrastructure::repositories {

class IUserRepository {
public:
    virtual ~IUserRepository() = default;

    virtual std::optional<User> find_by_id(int user_id) = 0;
    virtual std::optional<User> find_by_username(const std::string& username) = 0;
    virtual int save(const User& user) = 0;
};

}  // namespace infrastructure::repositories
