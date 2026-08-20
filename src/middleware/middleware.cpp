#include "middleware/middleware.h"

#include <utility>

namespace presentation::middleware {

namespace {

// 中间件链的遍历状态（栈上，execute() 栈帧内有效）：
// 链是同步递归执行，execute() 内 next() 返回时整条链已完成，
// 此时所有 handle 帧均已退出，无人再持有状态引用。
struct ChainState {
    MiddlewarePipeline* pipeline = nullptr;
    const presentation::http::HttpRequest* request = nullptr;
    presentation::http::HttpResponse* response = nullptr;
    std::function<void()> final_action;
    std::size_t index = 0;
};

}  // namespace

void MiddlewarePipeline::use(std::shared_ptr<Middleware> middleware) {
    if (middleware) {
        middlewares_.push_back(std::move(middleware));
    }
}

void MiddlewarePipeline::execute(const presentation::http::HttpRequest& request,
                                 presentation::http::HttpResponse& response,
                                 std::function<void()> final_action) {
    ChainState state;  // 纯栈上（对象池实测对 CPU/QPS 中性，已去除池化）
    ChainState* s = &state;

    s->pipeline = this;
    s->request = &request;
    s->response = &response;
    s->final_action = std::move(final_action);
    s->index = 0;

    // 闭包仅捕获 s（8B）+ &next（8B）= 16B，恰好落入 std::function 的
    // SBO 内联缓冲 → 不产生堆分配。handle 按值传 Next 时同样只是 16B 的 SBO 拷贝。
    Middleware::Next next = [s, &next]() {
        if (s->index < s->pipeline->middlewares_.size()) {
            auto current = s->pipeline->middlewares_[s->index++];
            current->handle(*s->request, *s->response, next);
        } else if (s->final_action) {
            s->final_action();
        }
    };
    next();  // 同步递归：返回时整条链已执行完毕
}

}  // namespace presentation::middleware
