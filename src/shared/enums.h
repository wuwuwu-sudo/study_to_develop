#pragma once
#include <iostream>
// 无需引入额外库

enum class UserRole {
    CUSTOMER,
    MERCHANT
};

enum class OrderStatus {
    PENDING,
    CONFIRMED,
    DELIVERING,
    DELIVERED,
    CANCELLED
};

enum class HttpMethod {
    GET,
    POST,
    PUT,
    DELETE,
    HEAD,
    OPTIONS,
    PATCH
};