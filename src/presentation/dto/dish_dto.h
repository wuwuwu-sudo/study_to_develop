#pragma once
#include <string>
#include <vector>
#include <iostream>
#include "shared/enums.h"

struct DishDto {
    int id = 0;
    int merchant_id = 0;
    std::string name;
    double price = 0.0;
    std::string category;
    std::string description;
    bool available = true;
};