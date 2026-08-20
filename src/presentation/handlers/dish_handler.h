#pragma once

#include "application/auth_service.h"
#include "application/dish_service.h"
#include "presentation/http/http_request.h"
#include "presentation/http/http_response.h"

namespace presentation::handlers {

class DishHandler {
public:
    DishHandler(
        application::DishService& dish_service,
        application::AuthService& auth_service
    );

    void handle_get_dishes(const presentation::http::HttpRequest& request,
                           presentation::http::HttpResponse& response);
    void handle_add_dish(const presentation::http::HttpRequest& request,
                         presentation::http::HttpResponse& response);
    void handle_edit_dish(const presentation::http::HttpRequest& request,
                          presentation::http::HttpResponse& response);
    void handle_toggle_available(const presentation::http::HttpRequest& request,
                                 presentation::http::HttpResponse& response);
    void handle_delete_dish(const presentation::http::HttpRequest& request,
                            presentation::http::HttpResponse& response);

private:
    application::DishService& dish_service_;
    application::AuthService& auth_service_;
};

}  // namespace presentation::handlers
