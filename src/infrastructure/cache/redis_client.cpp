#include "infrastructure/cache/redis_client.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cstring>
#include <utility>

namespace infrastructure::cache {

namespace {
using Clock = std::chrono::steady_clock;

constexpr size_t kMaxBulkLength = 64 * 1024 * 1024;  // 单条回复最大 64MB
constexpr int kMaxArrayElements = 100000;             // 防恶意超大数组
constexpr int kMaxNestingDepth = 16;                  // 防递归过深
}  // namespace

RedisClient::RedisClient(std::string host, int port,
                         int connect_timeout_ms, int read_timeout_ms)
    : host_(std::move(host))
    , port_(port)
    , connect_timeout_ms_(connect_timeout_ms > 0 ? connect_timeout_ms : 500)
    , read_timeout_ms_(read_timeout_ms > 0 ? read_timeout_ms : 500) {}

RedisClient::~RedisClient() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

bool RedisClient::available() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return fd_ >= 0;
}

bool RedisClient::ensure_connected_locked() {
    auto now = Clock::now();
    if (fd_ >= 0) {
        return true;
    }
    if (now < retry_after_) {
        return false;  // 退避期内，跳过连接尝试
    }

    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        retry_after_ = now + retry_backoff_;
        return false;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port_));
    if (::inet_pton(AF_INET, host_.c_str(), &addr.sin_addr) != 1) {
        // 非 IPv4 字面量则解析域名
        hostent* he = ::gethostbyname(host_.c_str());
        if (he == nullptr) {
            ::close(fd);
            retry_after_ = now + retry_backoff_;
            return false;
        }
        std::memcpy(&addr.sin_addr, he->h_addr, static_cast<size_t>(he->h_length));
    }

    // 非阻塞 connect + poll 实现连接超时
    int flags = ::fcntl(fd, F_GETFL, 0);
    ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    int rc = ::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    if (rc != 0 && errno != EINPROGRESS) {
        ::close(fd);
        retry_after_ = now + retry_backoff_;
        return false;
    }
    if (rc != 0) {
        pollfd pfd{fd, static_cast<short>(POLLOUT), 0};
        int pr = ::poll(&pfd, 1, connect_timeout_ms_);
        if (pr <= 0 || (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
            ::close(fd);
            retry_after_ = now + retry_backoff_;
            return false;
        }
    }

    // 恢复阻塞模式并设置读写超时
    ::fcntl(fd, F_SETFL, flags);
    timeval tv{};
    tv.tv_sec = read_timeout_ms_ / 1000;
    tv.tv_usec = (read_timeout_ms_ % 1000) * 1000;
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    fd_ = fd;
    inbuf_.clear();
    return true;
}

void RedisClient::fail_locked() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
    inbuf_.clear();
    retry_after_ = Clock::now() + retry_backoff_;
}

bool RedisClient::send_command_locked(const std::vector<std::string>& argv) {
    std::string cmd;
    cmd.reserve(64);
    cmd += '*';
    cmd += std::to_string(argv.size());
    cmd += "\r\n";
    for (const auto& arg : argv) {
        cmd += '$';
        cmd += std::to_string(arg.size());
        cmd += "\r\n";
        cmd += arg;
        cmd += "\r\n";
    }
    size_t sent = 0;
    while (sent < cmd.size()) {
        ssize_t n = ::send(fd_, cmd.data() + sent, cmd.size() - sent, MSG_NOSIGNAL);
        if (n <= 0) {
            fail_locked();
            return false;
        }
        sent += static_cast<size_t>(n);
    }
    return true;
}

bool RedisClient::read_line_locked(std::string& line) {
    line.clear();
    while (true) {
        auto pos = inbuf_.find("\r\n");
        if (pos != std::string::npos) {
            line = inbuf_.substr(0, pos);
            inbuf_.erase(0, pos + 2);
            return true;
        }
        if (inbuf_.size() > kMaxBulkLength) {
            fail_locked();
            return false;
        }
        char buf[4096];
        ssize_t r = ::recv(fd_, buf, sizeof(buf), 0);
        if (r <= 0) {
            fail_locked();
            return false;
        }
        inbuf_.append(buf, static_cast<size_t>(r));
    }
}

bool RedisClient::read_n_locked(std::string& out, size_t n) {
    out.clear();
    out.reserve(n);
    while (out.size() < n) {
        if (!inbuf_.empty()) {
            size_t take = std::min(n - out.size(), inbuf_.size());
            out.append(inbuf_, 0, take);
            inbuf_.erase(0, take);
            continue;
        }
        char buf[4096];
        ssize_t r = ::recv(fd_, buf, sizeof(buf), 0);
        if (r <= 0) {
            fail_locked();
            return false;
        }
        inbuf_.append(buf, static_cast<size_t>(r));
    }
    return true;
}

bool RedisClient::read_reply_locked(Reply& reply, int depth) {
    if (depth > kMaxNestingDepth) {
        fail_locked();
        return false;
    }
    std::string line;
    if (!read_line_locked(line) || line.empty()) {
        return false;
    }
    const char type = line[0];
    switch (type) {
        case '+':  // 简单字符串
            reply.type = Reply::Type::Status;
            reply.str = line.substr(1);
            return true;
        case '-':  // 错误
            reply.type = Reply::Type::Error;
            reply.str = line.substr(1);
            return true;
        case ':':  // 整数
            reply.type = Reply::Type::Integer;
            try {
                reply.integer = std::stoll(line.substr(1));
            } catch (const std::exception&) {
                fail_locked();
                return false;
            }
            return true;
        case '$': {  // 批量字符串
            long long len = 0;
            try {
                len = std::stoll(line.substr(1));
            } catch (const std::exception&) {
                fail_locked();
                return false;
            }
            if (len == -1) {
                reply.type = Reply::Type::Nil;
                return true;
            }
            if (len < 0 || static_cast<size_t>(len) > kMaxBulkLength) {
                fail_locked();
                return false;
            }
            std::string payload;
            if (!read_n_locked(payload, static_cast<size_t>(len))) {
                return false;
            }
            std::string crlf;
            if (!read_n_locked(crlf, 2) || crlf != "\r\n") {
                fail_locked();
                return false;
            }
            reply.type = Reply::Type::Bulk;
            reply.str = std::move(payload);
            return true;
        }
        case '*': {  // 数组
            long long count = 0;
            try {
                count = std::stoll(line.substr(1));
            } catch (const std::exception&) {
                fail_locked();
                return false;
            }
            if (count == -1) {
                reply.type = Reply::Type::Nil;
                return true;
            }
            if (count < 0 || count > kMaxArrayElements) {
                fail_locked();
                return false;
            }
            reply.type = Reply::Type::Array;
            reply.elements.resize(static_cast<size_t>(count));
            for (auto& el : reply.elements) {
                if (!read_reply_locked(el, depth + 1)) {
                    return false;
                }
            }
            return true;
        }
        default:
            fail_locked();
            return false;
    }
}

std::optional<std::string> RedisClient::get(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!ensure_connected_locked() || !send_command_locked({"GET", key})) {
        return std::nullopt;
    }
    Reply reply;
    if (!read_reply_locked(reply)) {
        return std::nullopt;
    }
    if (reply.type == Reply::Type::Nil) {
        return std::nullopt;
    }
    if (reply.type != Reply::Type::Bulk) {
        fail_locked();
        return std::nullopt;
    }
    return reply.str;
}

bool RedisClient::set(const std::string& key, const std::string& value, int ttl_seconds) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!ensure_connected_locked()) {
        return false;
    }
    std::vector<std::string> argv = {"SET", key, value};
    if (ttl_seconds > 0) {
        argv.push_back("EX");
        argv.push_back(std::to_string(ttl_seconds));
    }
    if (!send_command_locked(argv)) {
        return false;
    }
    Reply reply;
    if (!read_reply_locked(reply)) {
        return false;
    }
    if (reply.type == Reply::Type::Error) {
        fail_locked();
        return false;
    }
    return reply.type == Reply::Type::Status && reply.str == "OK";
}

bool RedisClient::del(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!ensure_connected_locked() || !send_command_locked({"DEL", key})) {
        return false;
    }
    Reply reply;
    if (!read_reply_locked(reply)) {
        return false;
    }
    if (reply.type == Reply::Type::Error) {
        fail_locked();
        return false;
    }
    return reply.type == Reply::Type::Integer && reply.integer >= 0;
}

int RedisClient::clear_prefix(const std::string& prefix) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!ensure_connected_locked()) {
        return -1;
    }
    std::string cursor = "0";
    int deleted = 0;
    do {
        std::vector<std::string> argv = {"SCAN", cursor, "MATCH", prefix + "*", "COUNT", "200"};
        if (!send_command_locked(argv)) {
            return -1;
        }
        Reply reply;
        if (!read_reply_locked(reply)) {
            return -1;
        }
        if (reply.type == Reply::Type::Error) {
            fail_locked();
            return -1;
        }
        if (reply.type != Reply::Type::Array || reply.elements.size() != 2 ||
            reply.elements[0].type != Reply::Type::Bulk ||
            reply.elements[1].type != Reply::Type::Array) {
            fail_locked();
            return -1;
        }
        cursor = reply.elements[0].str;

        std::vector<std::string> keys;
        for (const auto& k : reply.elements[1].elements) {
            if (k.type == Reply::Type::Bulk) {
                keys.push_back(k.str);
            }
        }
        if (keys.empty()) {
            continue;
        }
        std::vector<std::string> del_argv;
        del_argv.reserve(keys.size() + 1);
        del_argv.push_back("DEL");
        for (const auto& k : keys) {
            del_argv.push_back(k);
        }
        if (!send_command_locked(del_argv)) {
            return -1;
        }
        Reply del_reply;
        if (!read_reply_locked(del_reply)) {
            return -1;
        }
        if (del_reply.type == Reply::Type::Error) {
            fail_locked();
            return -1;
        }
        if (del_reply.type == Reply::Type::Integer) {
            deleted += static_cast<int>(del_reply.integer);
        }
    } while (cursor != "0");
    return deleted;
}

bool RedisClient::ping() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!ensure_connected_locked() || !send_command_locked({"PING"})) {
        return false;
    }
    Reply reply;
    if (!read_reply_locked(reply)) {
        return false;
    }
    if (reply.type == Reply::Type::Error) {
        fail_locked();
        return false;
    }
    return reply.type == Reply::Type::Status && reply.str == "PONG";
}

}  // namespace infrastructure::cache
