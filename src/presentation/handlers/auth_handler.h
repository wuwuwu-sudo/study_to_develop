#pragma once

#include "application/auth_service.h"
#include "infrastructure/session/session_manager.h"
#include "presentation/http/http_request.h"
#include "presentation/http/http_response.h"

namespace presentation::handlers {

class AuthHandler {
public:
    AuthHandler(
        application::AuthService& auth_service,
        infrastructure::session::SessionManager& session_manager
    );

    bool is_user_logged_in(const presentation::http::HttpRequest& request) const;
    bool is_merchant_logged_in(const presentation::http::HttpRequest& request) const;

    void handle_user_register(const presentation::http::HttpRequest& request,
                              presentation::http::HttpResponse& response);
    void handle_user_login(const presentation::http::HttpRequest& request,
                           presentation::http::HttpResponse& response);
    void handle_user_info(const presentation::http::HttpRequest& request,
                          presentation::http::HttpResponse& response);
    void handle_user_logout(const presentation::http::HttpRequest& request,
                            presentation::http::HttpResponse& response);
    void handle_merchant_register(const presentation::http::HttpRequest& request,
                                  presentation::http::HttpResponse& response);
    void handle_merchant_login(const presentation::http::HttpRequest& request,
                               presentation::http::HttpResponse& response);
    void handle_merchant_info(const presentation::http::HttpRequest& request,
                              presentation::http::HttpResponse& response);
    void handle_merchant_status(const presentation::http::HttpRequest& request,
                                presentation::http::HttpResponse& response);
    void handle_merchant_logout(const presentation::http::HttpRequest& request,
                                presentation::http::HttpResponse& response);
    void handle_get_shops(const presentation::http::HttpRequest& request,
                          presentation::http::HttpResponse& response);

private:
    application::AuthService& auth_service_;
    infrastructure::session::SessionManager& session_manager_;
};

}  // namespace presentation::handlers
