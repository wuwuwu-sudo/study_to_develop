#ifndef HANDLE_H_H
#define HANDLE_H
#include <sqlite3.h>
#include <string>
#include <cstring>
#include <ostream>
#include <iostream>
#include <uuid/uuid.h>
#include <ctime>
#include <sys/stat.h>
#include <fcntl.h>
#include <iomanip>
#include <random>
#include <sstream>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include "log.h"

using json = nlohmann::json;

bool save_user(const std::string& username, const std::string& password);


bool save_merchant(
    const std::string& username,
    const std::string& password,
    const std::string& shop_name,
    const std::string& address
);

bool get_cookie(const std::string& request,
                const std::string& name,
                std::string& value);

/* ---------- Session ---------- */
std::string generate_session_id();
int get_merchant_id_by_session(const std::string& request);
int get_user_id_by_session(const std::string& request);

/* ---------- 工具函数 ---------- */
bool starts_with(const char* str, const char* prefix);
bool ends_with(const char* str, const char* suffix);
const char* get_mime(const std::string& path);

/* ---------- HTTP 响应 ---------- */
void send_file(int fd, const char* path, const char* mime);
void send_json(int fd, const std::string& body);
void send_404(int fd);

/* ---------- 顾客 API ---------- */
void handle_user_register(int fd, const std::string& request);
void handle_user_login(int fd, const std::string& request);
void handle_user_info(int fd, const std::string& request);
void handle_my_orders(int fd, const std::string& request);
void handle_user_logout(int fd, const std::string& request);
void handle_get_shops(int fd, const std::string& request);

/* ---------- 商家 API ---------- */
void handle_merchant_register(int fd, const std::string& request);
void handle_merchant_login(int fd, const std::string& request);
void handle_merchant_info(int fd, const std::string& request);
void handle_merchant_status(int fd, const std::string& request);
void handle_merchant_logout(int fd, const std::string& request);
void handle_merchant_orders(int fd, const std::string& request);

/* ---------- 订单 API ---------- */
void handle_order_submit(int fd, const std::string& request);
void handle_update_order_status(int fd, const std::string& request);

#endif