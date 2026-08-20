#pragma once

#include "middleware/middleware.h"

namespace presentation::middleware {

class LoggingMiddleware : public Middleware {
public:
    explicit LoggingMiddleware(bool log_body = false);

    void handle(const presentation::http::HttpRequest& request,
                presentation::http::HttpResponse& response,
                Next next) override;

private:
    bool log_body_ = false;
};

}  // namespace presentation::middleware
