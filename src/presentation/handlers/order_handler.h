#pragma once

#include "application/auth_service.h"
#include "application/order_service.h"
#include "infrastructure/session/session_manager.h"
#include "presentation/http/http_request.h"
#include "presentation/http/http_response.h"

namespace presentation::handlers {

class OrderHandler {
public:
    OrderHandler(
        application::OrderService& order_service,
        application::AuthService& auth_service,
        infrastructure::session::SessionManager& session_manager
    );

    void handle_order_submit(const presentation::http::HttpRequest& request,
                             presentation::http::HttpResponse& response);
    void handle_update_order_status(const presentation::http::HttpRequest& request,
                                    presentation::http::HttpResponse& response);
    // 顾客确认收货（配送中 → 已完成）
    void handle_confirm_delivery(const presentation::http::HttpRequest& request,
                                 presentation::http::HttpResponse& response);
    void handle_my_orders(const presentation::http::HttpRequest& request,
                          presentation::http::HttpResponse& response);
    void handle_merchant_orders(const presentation::http::HttpRequest& request,
                                presentation::http::HttpResponse& response);

private:
    application::OrderService& order_service_;
    application::AuthService& auth_service_;
    infrastructure::session::SessionManager& session_manager_;
};

}  // namespace presentation::handlers
