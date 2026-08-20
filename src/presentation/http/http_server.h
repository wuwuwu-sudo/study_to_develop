#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include <sys/uio.h>
#include "middleware/middleware.h"
#include "presentation/http/http_parser.h"
#include "presentation/http/http_router.h"
#include "shared/delayed_shrink.h"
#include "shared/thread_pool.h"

namespace presentation::http {

class HttpServer {
public:
    explicit HttpServer(std::uint16_t port);

    void set_router(const HttpRouter& router);
    // 设置监听地址（默认 0.0.0.0；接入 Nginx 时应配置为 127.0.0.1，
    // 使后端仅可通过本机 Nginx 访问，不直接暴露到公网）
    void set_bind_address(const std::string& bind_address);
    void set_max_connections(int max_connections);
    void set_read_timeout(int seconds);
    void set_write_timeout(int seconds);
    // 设置处理客户端事件的线程池大小（0 = 不使用线程池，
    // 在事件循环线程内同步处理）
    void set_worker_count(int worker_count);
    void use(std::unique_ptr<presentation::middleware::Middleware> middleware);

    bool start();
    void stop();
    void event_loop();
    void handle_accept();
    // 兼容入口：按 fd 查找连接后处理（供事件循环之外的调用）
    void handle_client_data(int client_fd);

private:
    // 每个客户端连接的运行状态（keep-alive 长连接）
    struct ClientConn {
        int fd = -1;
        std::string buffer;  // 尚未处理的请求字节（跨事件累积）
        std::chrono::steady_clock::time_point last_active;
        bool closed = false;
        std::mutex mutex;  // 串行化同一连接的处理与关闭
        shared::DelayedShrink buffer_shrink;  // 连接缓冲的延迟缩容状态
    };

    std::shared_ptr<ClientConn> get_conn(int client_fd);
    void add_conn(std::shared_ptr<ClientConn> conn);
    void close_conn(std::shared_ptr<ClientConn> conn);
    void process_client(std::shared_ptr<ClientConn> conn);
    void sweep_idle_connections();
    // 循环 writev 发送多个缓冲（响应头/体）直到全部发出；超时/失败返回 false
    bool send_allv(int fd, const struct iovec* iov, int iovcnt, int timeout_ms);

    std::uint16_t port_;
    std::string bind_address_ = "0.0.0.0";  // 监听地址（默认全接口）
    HttpRouter router_;
    presentation::middleware::MiddlewarePipeline pipeline_;
    HttpParser parser_;  // 无状态，可安全共享
    int max_connections_ = 1024;
    int read_timeout_ = 30;
    int write_timeout_ = 30;
    int worker_count_ = 0;
    int listen_fd_ = -1;
    int epoll_fd_ = -1;
    bool running_ = false;
    std::unique_ptr<shared::ThreadPool> thread_pool_;

    // 连接表：fd -> 连接状态；按 fd 哈希分片，每片独立锁
    // （减少主线程读与 worker 清理写的争用），逐连接的 mutex 保护各自状态。
    static constexpr std::size_t kConnShards = 16;
    struct ConnShard {
        std::mutex mutex;
        std::unordered_map<int, std::shared_ptr<ClientConn>> conns;
    };
    std::array<ConnShard, kConnShards> conn_shards_;
    // 延迟缩容累计次数（观测复用缓冲是否在持续小用时释放了多余容量）
    std::atomic<std::size_t> shrink_count_{0};
};

}  // namespace presentation::http
