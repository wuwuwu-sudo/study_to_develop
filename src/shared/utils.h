#pragma once
#include <string>
#include <cstddef>
#include <iostream>

std::string generate_hex_id(std::size_t length);
std::string hash_password(const std::string& password);
bool verify_password(const std::string& password, const std::string& password_hash);
bool starts_with(const std::string& text, const std::string& prefix);
bool ends_with(const std::string& text, const std::string& suffix);
std::string trim(const std::string& text);
std::string url_decode(const std::string& text);
std::string get_cookie(const std::string& request, const std::string& name);