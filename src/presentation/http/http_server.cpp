#include "presentation/http/http_server.h"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <utility>

#include "infrastructure/common/logger.h"
#include "middleware/middleware.h"
#include "presentation/http/http_parser.h"
#include "shared/delayed_shrink.h"

namespace {

// 单个连接缓冲区的上限（防超长请求拖垮内存）
constexpr std::size_t kMaxRequestBuffer = 1 << 20;  // 1MB

// 单次调度批量读的兜底：
//   读取时间上限：连接持续灌数据时，最多读这么多时间就停止本批读取，
//   剩余数据留待下次 epoll 再读，避免单次调度长期占用 worker（读饥饿）；
//   读取量上限同理（防御性，防止一次性读入过多）。
constexpr auto kReadBatchTimeout = std::chrono::milliseconds(50);
constexpr std::size_t kMaxReadBytes = 256 * 1024;  // 256KB

// 检查 Connection 头是否含指定 token（大小写不敏感）
bool connection_has_token(const presentation::http::HttpRequest& req,
                          const char* token) {
    for (const auto& [key, value] : req.headers) {
        std::string lower_key = key;
        std::transform(lower_key.begin(), lower_key.end(), lower_key.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (lower_key != "connection") {
            continue;
        }
        std::string lower_value = value;
        std::transform(lower_value.begin(), lower_value.end(), lower_value.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return lower_value.find(token) != std::string::npos;
    }
    return false;
}

// 判定请求是否应保持长连接
bool request_keep_alive(const presentation::http::HttpRequest& req) {
    if (req.version == "HTTP/1.1") {
        // HTTP/1.1 默认 keep-alive，除非显式 Connection: close
        return !connection_has_token(req, "close");
    }
    // HTTP/1.0 默认短连接，仅显式 Connection: keep-alive 时长连接
    return connection_has_token(req, "keep-alive");
}

}  // namespace

namespace presentation::http {

HttpServer::HttpServer(std::uint16_t port)
    : port_(port) {}

void HttpServer::set_router(const HttpRouter& router) {
    router_ = router;
}

void HttpServer::set_bind_address(const std::string& bind_address) {
    bind_address_ = bind_address;
}

void HttpServer::set_max_connections(int max_connections) {
    max_connections_ = max_connections;
}

void HttpServer::set_read_timeout(int seconds) {
    read_timeout_ = seconds;
}

void HttpServer::set_write_timeout(int seconds) {
    write_timeout_ = seconds;
}

void HttpServer::set_task_queue_enabled(bool enabled) {
    task_queue_enabled_ = enabled;
}

void HttpServer::set_task_queue_consumers(int consumers) {
    task_queue_consumers_ = consumers > 0 ? consumers : 1;
}

void HttpServer::set_db_submitter(DbSubmitFn fn) {
    db_submit_ = std::move(fn);
}

void HttpServer::set_read_path_async(bool enabled) {
    read_path_async_ = enabled;
}

void HttpServer::set_slow_path_switch(std::shared_ptr<shared::SlowPathSwitch> sw) {
    slow_switch_ = std::move(sw);
}

void HttpServer::use(std::unique_ptr<presentation::middleware::Middleware> middleware) {
    if (middleware) {
        pipeline_.use(std::shared_ptr<presentation::middleware::Middleware>(std::move(middleware)));
    }
}

bool HttpServer::start() {
    listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) {
        return false;
    }

    int opt = 1;
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port_);
    // 监听地址：默认 0.0.0.0（兼容直连）；接入 Nginx 时配置为 127.0.0.1，
    // 使后端仅可通过本机 Nginx 访问，不直接暴露到公网
    if (inet_pton(AF_INET, bind_address_.c_str(), &addr.sin_addr) != 1) {
        // 非法/空地址回退到全接口，避免绑定失败导致服务不可用
        addr.sin_addr.s_addr = INADDR_ANY;
    }

    if (bind(listen_fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0 ||
        listen(listen_fd_, max_connections_) < 0) {
        return false;
    }

    epoll_fd_ = epoll_create1(0);
    if (epoll_fd_ < 0) {
        return false;
    }

    struct epoll_event ev {};
    ev.events = EPOLLIN;
    ev.data.fd = listen_fd_;
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, listen_fd_, &ev) < 0) {
        return false;
    }

    running_ = true;
    return true;
}

void HttpServer::stop() {
    running_ = false;
    if (listen_fd_ >= 0) {
        close(listen_fd_);
        listen_fd_ = -1;
    }
    if (epoll_fd_ >= 0) {
        close(epoll_fd_);
        epoll_fd_ = -1;
    }
    // 关闭所有残留的长连接（遍历全部分片收集快照后统一关闭）
    std::vector<std::shared_ptr<ClientConn>> conns;
    for (auto& shard : conn_shards_) {
        std::lock_guard<std::mutex> lock(shard.mutex);
        for (const auto& kv : shard.conns) {
            conns.push_back(kv.second);
        }
        shard.conns.clear();
    }
    for (auto& conn : conns) {
        std::lock_guard<std::mutex> cl(conn->mutex);
        if (conn->closed) {
            continue;
        }
        conn->closed = true;
        if (conn->fd >= 0) {
            close(conn->fd);
            conn->fd = -1;
        }
    }
}

void HttpServer::event_loop() {
    constexpr int MAX_EVENTS = 1024;
    struct epoll_event events[MAX_EVENTS];

    // 进入 epoll 循环前创建任务队列（消费线程数可配，默认 1），
    // 事件循环只负责 accept/读事件，业务处理提交到任务队列消费。
    if (task_queue_enabled_) {
        task_queue_ = std::make_unique<shared::TaskQueue>(task_queue_consumers_);
        LOG_INFO("Task queue created: " + std::to_string(task_queue_consumers_) +
                 " consumer thread(s)");
    }

    while (running_) {
        // 1s 周期超时：让事件循环能定期清扫空闲（死）连接，
        // 同时保持 epoll 对事件的即时响应
        int nfds = epoll_wait(epoll_fd_, events, MAX_EVENTS, 1000);
        if (nfds < 0) {
            if (running_) {
                stop();
            }
            break;
        }

        for (int i = 0; i < nfds; ++i) {
            const int fd = events[i].data.fd;
            if (fd == listen_fd_) {
                handle_accept();
                continue;
            }

            // 对端关闭 / 连接异常：直接从事件循环清理
            if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                auto conn = get_conn(fd);
                if (conn) {
                    close_conn(conn);
                }
                continue;
            }

            // 阶段3：可写事件——续发连接挂起的 out_buf（慢客户端场景），
            // 发完注销 EPOLLOUT 回 READY 并提交 process_client 继续处理。
            if (events[i].events & EPOLLOUT) {
                auto conn = get_conn(fd);
                if (conn) {
                    if (task_queue_) {
                        task_queue_->enqueue([this, conn] { flush_out(conn); });
                    } else {
                        flush_out(conn);
                    }
                }
            }

            // 正常可读：捕获 shared_ptr 提交任务队列，避免 fd 复用竞态。
            // （若与 EPOLLOUT 同时就绪会多调度一次 process_client；sending 时其内部自行返回，无害）
            if (events[i].events & EPOLLIN) {
                auto conn = get_conn(fd);
                if (!conn) {
                    continue;
                }
                if (task_queue_) {
                    task_queue_->enqueue([this, conn] { process_client(conn); });
                } else {
                    process_client(conn);
                }
            }
        }

        // 周期清扫空闲连接（心跳配合下的死连接清理）
        sweep_idle_connections();
    }

    // 退出循环后停止任务队列并等待任务全部完成，
    // 避免仍执行中的任务引用已销毁的服务器对象
    if (task_queue_) {
        task_queue_->shutdown();
        task_queue_->wait_all();
        task_queue_.reset();
        LOG_INFO("Task queue shut down");
    }
    LOG_INFO("Delayed shrink events: " + std::to_string(shrink_count_.load()));
}

void HttpServer::handle_accept() {
    struct sockaddr_in client_addr {};
    socklen_t addr_len = sizeof(client_addr);
    int client_fd = accept(listen_fd_, reinterpret_cast<struct sockaddr*>(&client_addr), &addr_len);
    if (client_fd < 0) {
        return;
    }

    int flags = fcntl(client_fd, F_GETFL, 0);
    fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);

    // ---- TCP keepalive（内核心跳探测）----
    // 内核定期发送探测包，对端无响应（掉线/宕机）时内核使 socket 报错，
    // 我们随后的 recv/send 就能感知并清理，避免死连接长期堆积。
    int keepalive = 1;
    setsockopt(client_fd, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sizeof(keepalive));
    int keep_idle = std::max(1, read_timeout_);        // 空闲多久开始探测
    int keep_intvl = std::min(std::max(5, read_timeout_ / 3), 30);  // 探测间隔
    int keep_cnt = 3;                                   // 连续无响应次数判死
    setsockopt(client_fd, IPPROTO_TCP, TCP_KEEPIDLE, &keep_idle, sizeof(keep_idle));
    setsockopt(client_fd, IPPROTO_TCP, TCP_KEEPINTVL, &keep_intvl, sizeof(keep_intvl));
    setsockopt(client_fd, IPPROTO_TCP, TCP_KEEPCNT, &keep_cnt, sizeof(keep_cnt));

    struct epoll_event ev {};
    // EPOLLRDHUP：对端关闭连接（含半关闭）时立即收到事件，无需等数据
    ev.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    ev.data.fd = client_fd;
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, client_fd, &ev) < 0) {
        close(client_fd);
        return;
    }

    auto conn = std::make_shared<ClientConn>();
    conn->fd = client_fd;
    conn->last_active = std::chrono::steady_clock::now();
    add_conn(conn);
}

void HttpServer::handle_client_data(int client_fd) {
    // 兼容入口：按 fd 查找连接并处理
    auto conn = get_conn(client_fd);
    if (conn) {
        process_client(conn);
    }
}

// ============================================================
// 连接表管理（按 fd 哈希分片，每片独立锁；逐连接 mutex 保护各自状态）
// ============================================================
std::shared_ptr<HttpServer::ClientConn> HttpServer::get_conn(int client_fd) {
    auto& shard = conn_shards_[static_cast<std::size_t>(client_fd) % kConnShards];
    std::lock_guard<std::mutex> lock(shard.mutex);
    auto it = shard.conns.find(client_fd);
    return it == shard.conns.end() ? nullptr : it->second;
}

void HttpServer::add_conn(std::shared_ptr<ClientConn> conn) {
    auto& shard = conn_shards_[static_cast<std::size_t>(conn->fd) % kConnShards];
    std::lock_guard<std::mutex> lock(shard.mutex);
    shard.conns[conn->fd] = std::move(conn);
}

// 关闭连接：从 epoll 注销、close(fd)、从连接表移除。幂等。
void HttpServer::close_conn(std::shared_ptr<ClientConn> conn) {
    int fd = -1;
    {
        std::lock_guard<std::mutex> lock(conn->mutex);
        if (conn->closed) {
            return;
        }
        conn->closed = true;
        fd = conn->fd;
        if (fd >= 0) {
            epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
            ::close(fd);
            conn->fd = -1;
        }
    }
    if (fd >= 0) {
        auto& shard = conn_shards_[static_cast<std::size_t>(fd) % kConnShards];
        std::lock_guard<std::mutex> lock(shard.mutex);
        shard.conns.erase(fd);
    }
}

// ============================================================
// 单连接处理：读 → 解析 → 中间件/路由 → 发送，然后保持长连接等待下一个请求。
// 使用 unique_lock 以便在需要关闭时先解锁再 close_conn（避免自锁）。
// ============================================================
void HttpServer::process_client(std::shared_ptr<ClientConn> conn) {
    std::unique_lock<std::mutex> lock(conn->mutex);
    if (conn->closed || conn->fd < 0) {
        return;
    }
    // 阶段3：连接处于 SENDING（out_buf 未发完）。保持响应顺序，本次不处理；
    // 由 flush_out 发完后提交 process_client 继续（届时会 recv 期间到达的新数据）。
    if (conn->sending) {
        return;
    }

    // ---- 阶段2 写请求异步挂起恢复 ----
    // DB 工作线程已完成写处理：发送缓存好的响应并保持连接。
    if (conn->db_pending) {
        if (conn->db_response.empty()) {
            // 响应尚未就绪（事件循环在写完成前又调度到本连接）：忽略本次调度，
            // 由 DB 完成后的 resume 任务负责发送。
            return;
        }
        conn->db_pending = false;
        std::string resp;
        resp.swap(conn->db_response);
        // 阶段3：非阻塞发送（写满则挂起 out_buf + EPOLLOUT，释放消费者线程）
        if (!try_send(conn, std::move(resp), false)) {
            if (conn->sending) {
                return;  // 已挂起：EPOLLOUT 就绪后由 flush_out 续发
            }
            conn->buffer.clear();
            lock.unlock();
            close_conn(conn);
            return;
        }
        conn->last_active = std::chrono::steady_clock::now();
        if (conn->buffer.empty()) {
            return;  // 无剩余缓冲，保持连接等待下一请求
        }
        // 有剩余缓冲（管道后续请求）：落入下方常规解析处理
    }

    conn->last_active = std::chrono::steady_clock::now();

    // 1. 批量读：非阻塞读到 EAGAIN（一次读入本批所有可用数据）；对端关闭/异常 fatal。
    //    加超时/读取量兜底：连接持续灌数据时最多读 kReadBatchTimeout 或 kMaxReadBytes
    //    就停止本批读取，剩余数据留待下次 epoll 再读，避免单次调度长期占用 worker。
    bool fatal = false;
    const auto read_deadline =
        std::chrono::steady_clock::now() + kReadBatchTimeout;
    std::size_t total_read = 0;
    while (true) {
        char buffer[4096];
        ssize_t n = ::recv(conn->fd, buffer, sizeof(buffer), 0);
        if (n > 0) {
            conn->buffer.append(buffer, static_cast<size_t>(n));
            total_read += static_cast<size_t>(n);
            if (conn->buffer.size() > kMaxRequestBuffer) {
                fatal = true;  // 超长请求，防御性关闭
                break;
            }
            // 读超时/读取量兜底：达到上限停止本批读取（剩余下次再读）
            if (total_read >= kMaxReadBytes ||
                std::chrono::steady_clock::now() >= read_deadline) {
                break;
            }
            continue;
        }
        if (n == 0) {
            fatal = true;  // 对端已关闭
            break;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            break;  // 本批已读完
        }
        fatal = true;  // ECONNRESET 等
        break;
    }
    if (fatal) {
        lock.unlock();
        close_conn(conn);
        return;
    }

    // 2. 批量处理缓冲中的所有完整请求（天然支持 HTTP 管道 / 连续请求），
    //    响应累积到 send_buf，循环结束后一次批量发出（批量发，减少 writev 调用）。
    //    复用每线程的 request/response 与序列化/发送缓冲：headers/query_params 的
    //    桶数组与输出缓冲跨请求保留，减少每请求的重复分配（分配次数 ↓）。
    //    安全前提：本函数持有 conn->mutex，同一连接串行处理；所有 handler/
    //    中间件均同步使用引用、不留存，线程内复用不产生数据竞争。
    static thread_local HttpRequest request;
    static thread_local HttpResponse response;
    static thread_local std::string output;    // 响应头序列化缓冲（复用容量）
    static thread_local std::string send_buf;  // 本批所有响应的批量发送缓冲
    // 延迟缩容：复用缓冲持续小用时释放多余容量，防线程级缓冲长期占住峰值内存
    static thread_local shared::DelayedShrink shrink_output;
    static thread_local shared::DelayedShrink shrink_send_buf;
    static thread_local shared::DelayedShrink shrink_req_body;
    static thread_local shared::DelayedShrink shrink_req_headers;
    static thread_local shared::DelayedShrink shrink_resp_body;
    static thread_local shared::DelayedShrink shrink_resp_headers;
    request.headers.reserve(16);
    request.query_params.reserve(8);
    response.headers.reserve(8);
    send_buf.clear();

    bool need_close = false;
    while (true) {
        // 复位请求（clear 保留桶容量，仅释放节点，下次插入复用桶数组）
        request.method = HttpMethod::GET;
        request.path.clear();
        request.version = "HTTP/1.1";
        request.body.clear();
        request.headers.clear();
        request.query_params.clear();

        std::size_t consumed = 0;
        int rc = parser_.parse_one(conn->buffer, consumed, request);
        if (rc == 0) {
            break;  // 数据不完整，保持连接等待更多数据
        }
        if (rc < 0) {
            conn->buffer.clear();
            lock.unlock();
            close_conn(conn);
            return;
        }
        conn->buffer.erase(0, consumed);

        // 复位响应
        response.status_code = 200;
        response.headers.clear();
        response.body.clear();

        // ---- 阶段2 写请求异步化 / 阶段5 读路径异步化（整链投递，同步链之前拦截）----
        // 拦截条件：本批尚无已生成响应（send_buf 为空，请求在批首）、配置了 DB
        // 工作线程池；非 GET 请求始终异步（阶段2 写异步化）；GET 读请求仅在
        // server.read_path_async 开启时异步（阶段5 全链路事件驱动）：
        //   - 连接挂起（db_pending=true），立即释放 conn->mutex 返回，不阻塞事件循环；
        //   - worker 线程在请求副本上执行 中间件+路由（含 DB 读写 + 缓存失效/回填），
        //     产出自包含响应字节；完成后写回 conn->db_response 并把 conn 投回
        //     任务队列续跑（函数顶部恢复块发送响应）。
        //   读路径默认缓存命中率高保持同步（L1 吸收），避免每请求线程切换；
        //   开启读异步（read_path_async 强制 或 slow_path_switch 自适应检出慢路径）后
        //   L1 miss（L2/L3 慢或故障）不再阻塞消费者线程。
        if (send_buf.empty() && db_submit_ &&
            (request.method != HttpMethod::GET || read_path_async_ ||
             (slow_switch_ && slow_switch_->async()))) {
            // 捕获请求副本（thread_local 缓冲不跨线程）
            HttpRequest req_copy = request;
            conn->db_pending = true;
            std::shared_ptr<ClientConn> conn_sp = conn;  // 延长生命周期至完成
            lock.unlock();
            db_submit_(
                [this, req_copy = std::move(req_copy)]() -> std::string {
                    // worker 线程：完整执行中间件+路由，返回序列化响应字节
                    // static thread_local（静态存储期）可直接访问，无需捕获
                    static thread_local HttpResponse resp_async;
                    static thread_local std::string out_async;
                    // 阶段5 增强：异步模式下由 worker 上报读耗时给慢路径开关
                    std::chrono::steady_clock::time_point t0;
                    if (req_copy.method == HttpMethod::GET && slow_switch_) {
                        t0 = std::chrono::steady_clock::now();
                    }
                    resp_async.status_code = 200;
                    resp_async.headers.clear();
                    resp_async.body.clear();
                    out_async.clear();
                    pipeline_.execute(req_copy, resp_async,
                                      [this, &req_copy]() {
                                          router_.dispatch(req_copy,
                                                           resp_async);
                                      });
                    bool keep_alive = request_keep_alive(req_copy);
                    resp_async.set_header("Connection",
                                          keep_alive ? "keep-alive" : "close");
                    resp_async.serialize_header_into(out_async);
                    out_async.append(resp_async.body);
                    if (req_copy.method == HttpMethod::GET && slow_switch_) {
                        auto us = std::chrono::duration_cast<
                            std::chrono::microseconds>(
                                std::chrono::steady_clock::now() - t0)
                            .count();
                        slow_switch_->observe(static_cast<int>(us));
                    }
                    return out_async;
                },
                [this, conn_sp = std::move(conn_sp)](std::string resp) mutable {
                    // 完成回调（worker 线程）：写回响应并投回任务队列续跑
                    {
                        std::lock_guard<std::mutex> lk(conn_sp->mutex);
                        conn_sp->db_response = std::move(resp);
                    }
                    if (task_queue_) {
                        task_queue_->enqueue([this, conn_sp]() {
                            process_client(conn_sp);
                        });
                    } else {
                        // 无任务队列时退化为直接续跑（worker 线程内处理）
                        process_client(conn_sp);
                    }
                });
            return;  // 挂起连接，等待 DB 完成后的 resume
        }

        // 阶段5 增强：同步快路径下由消费者线程上报读耗时给慢路径开关
        //（仅 GET 且启用了自适应开关时测量；稳态 L1 命中耗时远低于阈值）
        std::chrono::steady_clock::time_point read_t0;
        const bool measure_read = request.method == HttpMethod::GET && slow_switch_;
        if (measure_read) {
            read_t0 = std::chrono::steady_clock::now();
        }
        // 执行中间件链（日志/限流/认证）+ 路由分发
        pipeline_.execute(request, response, [this, &request, &response]() {
            router_.dispatch(request, response);
        });

        // 判定是否长连接：HTTP/1.1 默认 keep-alive，除非显式 Connection: close；
        // HTTP/1.0 仅在显式 Connection: keep-alive 时长连接
        bool keep_alive = request_keep_alive(request);
        response.set_header("Connection", keep_alive ? "keep-alive" : "close");

        // 累积本响应到批量发送缓冲（响应头 + 响应体，含 Content-Length）
        response.serialize_header_into(output);
        send_buf.append(output);
        send_buf.append(response.body);
        conn->last_active = std::chrono::steady_clock::now();
        if (measure_read) {
            auto us = std::chrono::duration_cast<std::chrono::microseconds>(
                          std::chrono::steady_clock::now() - read_t0)
                          .count();
            slow_switch_->observe(static_cast<int>(us));
        }

        // ---- 延迟缩容：复用缓冲持续小用则释放多余容量（防占住峰值内存）----
        if (shrink_output.shrink_if_needed(output)) ++shrink_count_;
        if (shrink_send_buf.shrink_if_needed(send_buf)) ++shrink_count_;
        if (shrink_req_body.shrink_if_needed(request.body)) ++shrink_count_;
        if (shrink_resp_body.shrink_if_needed(response.body)) ++shrink_count_;
        if (shrink_req_headers.shrink_if_needed(request.headers)) ++shrink_count_;
        if (shrink_resp_headers.shrink_if_needed(response.headers)) ++shrink_count_;
        if (conn->buffer_shrink.shrink_if_needed(conn->buffer)) ++shrink_count_;

        if (!keep_alive) {
            need_close = true;  // 收到 Connection: close，停止处理本批后续请求
            break;
        }
    }

    // 3. 批量发送：本批所有响应一次发出。
    //    阶段3：非阻塞发送，写满则把剩余挂到 conn->out_buf 并注册 EPOLLOUT，
    //    立即返回释放消费者线程（慢客户端不再阻塞同线程其他连接）。
    if (!send_buf.empty()) {
        if (!try_send(conn, std::move(send_buf), need_close)) {
            if (conn->sending) {
                return;  // 已挂起：EPOLLOUT 就绪后 flush_out 续发，发完提交 process_client
            }
            conn->buffer.clear();
            lock.unlock();
            close_conn(conn);
            return;
        }
    }
    if (need_close) {
        lock.unlock();
        close_conn(conn);
        return;
    }
    // 连接保持打开，等待下一个请求（epoll 在新数据到来时再次调度本连接；
    // 数据不完整时 conn->buffer 留有余量，下次调度继续解析）
}

// ============================================================
// 死连接清理：配合 TCP keepalive 心跳，定期关闭空闲超时的连接，
// 防止半开/僵尸连接长期堆积。
// ============================================================
void HttpServer::sweep_idle_connections() {
    const auto now = std::chrono::steady_clock::now();
    std::vector<std::shared_ptr<ClientConn>> snapshot;
    for (auto& shard : conn_shards_) {
        std::lock_guard<std::mutex> lock(shard.mutex);
        snapshot.reserve(snapshot.size() + shard.conns.size());
        for (const auto& kv : shard.conns) {
            snapshot.push_back(kv.second);
        }
    }
    for (auto& conn : snapshot) {
        bool stale = false;
        {
            std::lock_guard<std::mutex> cl(conn->mutex);
            if (conn->closed) {
                continue;
            }
            if (conn->sending) {
                // 阶段3：发送挂起超时（慢客户端久未可写）→ 按写超时兜底关闭
                stale = now >= conn->send_deadline;
            } else {
                stale = now - conn->last_active >= std::chrono::seconds(read_timeout_);
            }
        }
        if (stale) {
            close_conn(conn);
        }
    }
}

// ============================================================
// 阶段3：发送非阻塞化（EPOLLOUT 事件驱动）
//
// 目标：慢客户端 / 大响应的发送不再占用消费者线程（消除 head-of-line blocking）。
// 机制：
//   - try_send：循环 send 直到全部发出或 EAGAIN；写满时把剩余挂到 conn->out_buf、
//     注册 EPOLLOUT 并置 sending=true，调用方立即返回释放线程。
//   - flush_out：EPOLLOUT 就绪时续发 out_buf；发完注销 EPOLLOUT、置 sending=false
//     回 READY，并提交 process_client 继续处理（flush 期间到达的新数据 / 剩余缓冲）。
//   - 超时兜底：send_deadline 超时由 sweep_idle_connections 关闭连接。
// 线程安全：所有状态在 conn->mutex 下访问（process_client / flush_out / close_conn 串行）。
// ============================================================
bool HttpServer::try_send(std::shared_ptr<ClientConn> conn, std::string&& data,
                          bool close_after) {
    // 前置：conn->mutex 已由调用方持有
    if (conn->closed || conn->fd < 0) {
        return false;
    }
    if (data.empty()) {
        return true;
    }
    std::size_t off = 0;
    while (off < data.size()) {
        ssize_t n = ::send(conn->fd, data.data() + off, data.size() - off,
                           MSG_NOSIGNAL);
        if (n > 0) {
            off += static_cast<std::size_t>(n);
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            // 写满：剩余挂到连接，注册 EPOLLOUT，等可写边沿续发
            conn->out_buf = data.substr(off);
            conn->sending = true;
            conn->close_after_send = close_after;
            conn->send_deadline =
                std::chrono::steady_clock::now() +
                std::chrono::seconds(write_timeout_);
            mod_epoll_out(conn->fd, true);
            return false;
        }
        return false;  // EPIPE / ECONNRESET / 对端关闭
    }
    return true;
}

void HttpServer::flush_out(std::shared_ptr<ClientConn> conn) {
    std::unique_lock<std::mutex> lock(conn->mutex);
    if (conn->closed || conn->fd < 0 || !conn->sending) {
        return;
    }
    // 发送超时兜底：超过 write_timeout 仍未发完 → 关闭
    if (std::chrono::steady_clock::now() >= conn->send_deadline) {
        lock.unlock();
        close_conn(conn);
        return;
    }
    bool close_after = conn->close_after_send;
    std::string data = std::move(conn->out_buf);
    if (!try_send(conn, std::move(data), close_after)) {
        if (conn->sending) {
            return;  // 仍写满：等下次 EPOLLOUT
        }
        lock.unlock();
        close_conn(conn);
        return;
    }
    // 全部发出：回 READY（注销 EPOLLOUT）
    conn->sending = false;
    conn->out_buf.clear();
    mod_epoll_out(conn->fd, false);
    conn->last_active = std::chrono::steady_clock::now();
    if (close_after) {
        lock.unlock();
        close_conn(conn);
        return;
    }
    // 回 READY：提交 process_client 处理 flush 期间到达的新数据 / 剩余缓冲
    //（ET 模式下这部分数据没有新边沿，必须主动调度，否则会卡住）
    lock.unlock();
    submit_process(conn);
}

void HttpServer::mod_epoll_out(int fd, bool enable) {
    struct epoll_event ev {};
    std::uint32_t events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    if (enable) {
        events |= EPOLLOUT;
    }
    ev.events = events;
    ev.data.fd = fd;
    // 连接可能已关闭（fd 失效）：MOD 失败（EBADF）可忽略
    epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &ev);
}

void HttpServer::submit_process(std::shared_ptr<ClientConn> conn) {
    if (task_queue_) {
        task_queue_->enqueue([this, conn] { process_client(conn); });
    } else {
        process_client(conn);
    }
}

}  // namespace presentation::http
