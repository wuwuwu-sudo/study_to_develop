#pragma once

#include <string>
#include <unordered_map>
#include "shared/enums.h"

namespace presentation::http {

class HttpRequest {
public:
    HttpMethod method = HttpMethod::GET;
    std::string path;
    std::string version = "HTTP/1.1";
    std::string body;
    std::unordered_map<std::string, std::string> headers;
    std::unordered_map<std::string, std::string> query_params;

    const std::string& get_path() const;
    std::string header(const std::string& name) const;
    std::string query(const std::string& name) const;
};

}  // namespace presentation::http
