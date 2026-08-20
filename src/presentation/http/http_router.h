#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <vector>
#include "shared/enums.h"
#include "presentation/http/http_request.h"
#include "presentation/http/http_response.h"

namespace presentation::http {

class HttpRouter {
public:
    using Handler = std::function<void(const HttpRequest&, HttpResponse&)>;

    void get(const std::string& path, Handler handler);
    void post(const std::string& path, Handler handler);
    void put(const std::string& path, Handler handler);
    void delete_route(const std::string& path, Handler handler);
    void set_not_found_handler(Handler handler);

    std::size_t count() const;
    bool dispatch(const HttpRequest& request, HttpResponse& response) const;

private:
    struct Route {
        HttpMethod method;
        std::string pattern;
        Handler handler;
    };

    std::vector<Route> routes_;
    Handler not_found_handler_;
};

}  // namespace presentation::http
