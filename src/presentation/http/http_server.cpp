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

void HttpServer::set_worker_count(int worker_count) {
    worker_count_ = worker_count > 0 ? worker_count : 0;
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

    // 进入 epoll 循环前创建线程池，用于并行处理客户端事件
    if (worker_count_ > 0) {
        thread_pool_ = std::make_unique<shared::ThreadPool>(worker_count_);
        LOG_INFO("Thread pool created: " + std::to_string(worker_count_) + " workers");
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

            // 正常可读：捕获 shared_ptr 提交线程池，避免 fd 复用竞态
            auto conn = get_conn(fd);
            if (!conn) {
                continue;
            }
            if (thread_pool_) {
                thread_pool_->enqueue([this, conn] { process_client(conn); });
            } else {
                process_client(conn);
            }
        }

        // 周期清扫空闲连接（心跳配合下的死连接清理）
        sweep_idle_connections();
    }

    // 退出循环后停止线程池并等待任务全部完成，
    // 避免仍执行中的任务引用已销毁的服务器对象
    if (thread_pool_) {
        thread_pool_->shutdown();
        thread_pool_->wait_all();
        thread_pool_.reset();
        LOG_INFO("Thread pool shut down");
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

    // 3. 批量发送：本批所有响应一次 writev 发出（发送超时兜底在 send_allv 内）
    if (!send_buf.empty()) {
        struct iovec iov[1];
        iov[0].iov_base = send_buf.data();
        iov[0].iov_len = send_buf.size();
        if (!send_allv(conn->fd, iov, 1, write_timeout_ * 1000)) {
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
            stale = !conn->closed &&
                    now - conn->last_active >= std::chrono::seconds(read_timeout_);
        }
        if (stale) {
            close_conn(conn);
        }
    }
}

// ============================================================
// 循环 writev 发送直到全部发出（非阻塞 + 短轮询）。
// 把响应头与响应体拆成多个 iovec 一次发出，省掉把 body 拼进整串的一次拷贝；
// 处理部分写入（跨 iovec 推进偏移），修复原实现忽略 send 返回值导致大响应被截断的问题。
// ============================================================
bool HttpServer::send_allv(int fd, const struct iovec* iov, int iovcnt,
                           int timeout_ms) {
    std::size_t total = 0;
    for (int i = 0; i < iovcnt; ++i) {
        total += iov[i].iov_len;
    }
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    std::size_t sent = 0;
    while (sent < total) {
        // 从 (iov, sent) 构造剩余未发送部分的 iovec 数组（跳过已发送的段）
        struct iovec rem[16];
        int rem_cnt = 0;
        std::size_t offset = sent;
        for (int i = 0; i < iovcnt && rem_cnt < 16; ++i) {
            if (offset >= iov[i].iov_len) {
                offset -= iov[i].iov_len;
                continue;
            }
            rem[rem_cnt].iov_base =
                static_cast<unsigned char*>(iov[i].iov_base) + offset;
            rem[rem_cnt].iov_len = iov[i].iov_len - offset;
            offset = 0;
            ++rem_cnt;
        }
        ssize_t n = ::writev(fd, rem, rem_cnt);
        if (n > 0) {
            sent += static_cast<std::size_t>(n);
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            if (std::chrono::steady_clock::now() >= deadline) {
                return false;  // 发送超时
            }
            pollfd pfd{fd, POLLOUT, 0};
            ::poll(&pfd, 1, 50);  // 短等待可写，避免忙等
            continue;
        }
        return false;  // EPIPE / ECONNRESET / 对端关闭
    }
    return true;
}

}  // namespace presentation::http
