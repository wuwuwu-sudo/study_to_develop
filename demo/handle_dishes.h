#ifndef HANDLE_DISHES_H
#define HANDLE_DISHES_H
#include "handle_user_and_merchant.h"

/* ---------- 菜品 API ---------- */
void handle_get_dishes(int fd, const std::string& request);
void handle_add_dish(int fd, const std::string& request);
void handle_edit_dish(int fd, const std::string& request);
void handle_dish_available(int fd, const std::string& request);
void handle_delete_dish(int fd, const std::string& request);

#endif