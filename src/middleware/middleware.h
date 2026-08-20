#pragma once

#include <functional>
#include <memory>
#include <vector>
#include "presentation/http/http_request.h"
#include "presentation/http/http_response.h"

namespace presentation::middleware {

class Middleware {
public:
    using Next = std::function<void()>;

    virtual ~Middleware() = default;
    virtual void handle(const presentation::http::HttpRequest& request,
                        presentation::http::HttpResponse& response,
                        Next next) = 0;
};

class MiddlewarePipeline {
public:
    void use(std::shared_ptr<Middleware> middleware);
    // final_action：中间件链全部放行后执行的最终处理（如路由分发）
    void execute(const presentation::http::HttpRequest& request,
                 presentation::http::HttpResponse& response,
                 std::function<void()> final_action = nullptr);

private:
    std::vector<std::shared_ptr<Middleware>> middlewares_;
};

}  // namespace presentation::middleware
