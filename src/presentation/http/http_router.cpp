#include "presentation/http/http_router.h"

namespace presentation::http {

void HttpRouter::get(const std::string& path, Handler handler) {
    routes_.push_back({HttpMethod::GET, path, std::move(handler)});
}

void HttpRouter::post(const std::string& path, Handler handler) {
    routes_.push_back({HttpMethod::POST, path, std::move(handler)});
}

void HttpRouter::put(const std::string& path, Handler handler) {
    routes_.push_back({HttpMethod::PUT, path, std::move(handler)});
}

void HttpRouter::delete_route(const std::string& path, Handler handler) {
    routes_.push_back({HttpMethod::DELETE, path, std::move(handler)});
}

void HttpRouter::set_not_found_handler(Handler handler) {
    not_found_handler_ = std::move(handler);
}

std::size_t HttpRouter::count() const {
    return routes_.size();
}

bool HttpRouter::dispatch(const HttpRequest& request, HttpResponse& response) const {
    for (const auto& route : routes_) {
        bool method_matches = route.method == request.method;
        bool exact_matches = route.pattern == request.path;
        bool wildcard_matches = route.pattern.size() > 1 && route.pattern[0] == '*' &&
                                request.path.size() >= route.pattern.size() - 1 &&
                                request.path.compare(request.path.size() - (route.pattern.size() - 1),
                                                     route.pattern.size() - 1,
                                                     route.pattern.c_str() + 1) == 0;

        if (method_matches && (exact_matches || wildcard_matches)) {
            route.handler(request, response);
            return true;
        }
    }

    if (not_found_handler_) {
        not_found_handler_(request, response);
        return true;
    }

    response.set_status(404);
    response.set_body("Not Found");
    return false;
}

}  // namespace presentation::http
