#pragma once

#include <string>
#include <vector>
#include "middleware/middleware.h"
#include "infrastructure/session/session_manager.h"

namespace presentation::middleware {

class AuthMiddleware : public Middleware {
public:
    explicit AuthMiddleware(infrastructure::session::SessionManager& session_manager);

    AuthMiddleware& cookie_name(const std::string& name);
    AuthMiddleware& protect(const std::string& path);
    AuthMiddleware& public_path(const std::string& path);

    void handle(const presentation::http::HttpRequest& request,
                presentation::http::HttpResponse& response,
                Next next) override;

private:
    infrastructure::session::SessionManager& session_manager_;
    std::string cookie_name_ = "session_id";
    std::vector<std::string> protected_paths_;
    std::vector<std::string> public_paths_;
};

}  // namespace presentation::middleware
