#pragma once
#include <string>
#include <vector>
#include <iostream>
#include "shared/enums.h"

struct UserDto {
    int id = 0;
    std::string username;
    UserRole role = UserRole::CUSTOMER;
    bool active = true;
    std::string created_at;
};
