#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
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
#include "shared/slow_path_switch.h"
#include "shared/task_queue.h"

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
    // 启用/禁用任务队列（true = 事件循环把客户端处理任务提交到
    // 任务队列；false = 事件循环线程内同步处理）
    void set_task_queue_enabled(bool enabled);
    // 任务队列消费线程数（默认 1；>1 可缓解单任务阻塞，需权衡过订阅）
    void set_task_queue_consumers(int consumers);
    // 阶段2 DB 工作线程池提交器：写请求异步化（见 process_client）。
    // submit(task, on_done)：在 worker 线程执行 task()（返回响应字节），完成后回调 on_done。
    using DbSubmitFn = std::function<void(std::function<std::string()>,
                                          std::function<void(std::string)>)>;
    void set_db_submitter(DbSubmitFn fn);
    // 阶段5 读路径异步化强制开关（true = GET 读请求也投 DB 工作线程池整链执行）
    void set_read_path_async(bool enabled);
    // 阶段5 增强：读路径自适应慢路径开关（nullptr = 不启用）。
    // 常态同步快路径；detect 到慢样本（读请求耗时超阈值）自动切异步，缓解后切回。
    void set_slow_path_switch(std::shared_ptr<shared::SlowPathSwitch> sw);
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
        // 阶段2 写请求异步化：DB 工作线程处理中（连接挂起），响应待发
        bool db_pending = false;
        std::string db_response;  // DB 工作线程产出的待发送响应字节（mutex 保护）
        // 阶段3 发送非阻塞化：EPOLLOUT 事件驱动（慢客户端发送不再阻塞消费者线程）
        bool sending = false;      // 处于 SENDING 状态（out_buf 有待发数据，已注册 EPOLLOUT）
        std::string out_buf;       // 待发送缓冲（socket 写满后挂起，EPOLLOUT 就绪续发）
        bool close_after_send = false;  // 发完 out_buf 后关闭连接（Connection: close）
        std::chrono::steady_clock::time_point send_deadline;  // 发送超时时刻（超时兜底关闭）
    };

    std::shared_ptr<ClientConn> get_conn(int client_fd);
    void add_conn(std::shared_ptr<ClientConn> conn);
    void close_conn(std::shared_ptr<ClientConn> conn);
    void process_client(std::shared_ptr<ClientConn> conn);
    void sweep_idle_connections();
    // 阶段3：EPOLLOUT 就绪时续发连接挂起的 out_buf（发完注销 EPOLLOUT 回 READY）
    void flush_out(std::shared_ptr<ClientConn> conn);
    // 注册/注销 EPOLLOUT（追加到当前事件掩码；连接已关闭时 MOD 失败可忽略）
    void mod_epoll_out(int fd, bool enable);
    // 把 process_client 提交到任务队列（task_queue 禁用时事件循环线程内直接处理）
    void submit_process(std::shared_ptr<ClientConn> conn);
    // 阶段3 非阻塞发送：循环 send 直到全部发出或 EAGAIN。
    // 前置：调用方须持有 conn->mutex。返回 true = 已全部发出；
    //       返回 false 且 conn->sending == true = 已挂起（剩余存入 out_buf，注册 EPOLLOUT）；
    //       返回 false 且 conn->sending == false = 发送错误（EPIPE 等，需关闭连接）。
    bool try_send(std::shared_ptr<ClientConn> conn, std::string&& data, bool close_after);

    std::uint16_t port_;
    std::string bind_address_ = "0.0.0.0";  // 监听地址（默认全接口）
    HttpRouter router_;
    presentation::middleware::MiddlewarePipeline pipeline_;
    HttpParser parser_;  // 无状态，可安全共享
    int max_connections_ = 1024;
    int read_timeout_ = 30;
    int write_timeout_ = 30;
    bool task_queue_enabled_ = false;
    int task_queue_consumers_ = 1;
    // 阶段2 DB 工作线程池提交器（null = 关闭写请求异步化，全部同步处理）
    DbSubmitFn db_submit_;
    // 阶段5 读路径异步化强制开关（true = GET 读请求也走整链异步投递）
    bool read_path_async_ = false;
    // 阶段5 增强：读路径自适应慢路径开关（nullptr = 不启用；常态同步，慢则自动切异步）
    std::shared_ptr<shared::SlowPathSwitch> slow_switch_;
    int listen_fd_ = -1;
    int epoll_fd_ = -1;
    bool running_ = false;
    std::unique_ptr<shared::TaskQueue> task_queue_;

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
