#pragma once

#include <memory>
#include "middleware/middleware.h"

namespace presentation::middleware {

class RateLimitMiddleware : public Middleware {
public:
    struct Config {
        int max_requests = 100;
        int window_seconds = 60;
        int block_seconds = 300;
    };

    RateLimitMiddleware();
    explicit RateLimitMiddleware(const Config& config);
    ~RateLimitMiddleware() override;

    void handle(const presentation::http::HttpRequest& request,
                presentation::http::HttpResponse& response,
                Next next) override;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace presentation::middleware
