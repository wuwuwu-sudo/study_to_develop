#include "infrastructure/common/logger.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <ctime>
#include <deque>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>


// ============================================================
// 日志时间戳每秒缓存：
//   原实现每条日志都在调用线程执行 localtime_r + put_time，
//   perf 实测每次 localtime_r 都会触发 tz 文件解析(约 5% 用户态 CPU)
//   + 时间戳格式化(约 7%)。这里按「秒」每线程缓存一次格式化结果：
//   同一秒内的日志直接复用字符串，消除 tz 解析与格式化开销。
//   行缓冲同样线程局部复用，避免每行新建 ostringstream 的堆分配。
// ============================================================
namespace {
struct CachedTimestamp {
    std::time_t second = -1;   // 已缓存的是哪一秒（-1 = 未初始化）
    char buf[20] = {};         // "YYYY-MM-DD HH:MM:SS"（19 字符 + NUL）
};
thread_local CachedTimestamp g_cached_ts;
thread_local std::string g_line;  // 行缓冲：跨日志复用容量，避免反复分配

// 返回当前秒的格式化时间戳（同一秒内线程内缓存命中）
const char* cached_timestamp() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    if (g_cached_ts.second != t) {
        std::tm tm_buf{};
        localtime_r(&t, &tm_buf);  // 仅每秒每线程解析一次时区
        std::strftime(g_cached_ts.buf, sizeof(g_cached_ts.buf),
                      "%Y-%m-%d %H:%M:%S", &tm_buf);
        g_cached_ts.second = t;
    }
    return g_cached_ts.buf;
}
}  // namespace


//异步日志：调用线程只负责格式化并入队（无 I/O），
//后台写线程批量落盘，避免每个请求同步等待磁盘 I/O。
namespace infrastructure::common {

class LoggerImpl {
public:
    LoggerImpl() = default;

    ~LoggerImpl() {
        // 兜底：即使调用方未显式 shutdown（如异常退出路径），
        // 也要回收写线程，避免 joinable 的 std::thread 析构触发 std::terminate
        shutdown();
    }

    void initialize(LogLevel level, const std::string& filename, bool console_output,
                    int sampling) {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        level_.store(static_cast<int>(level), std::memory_order_relaxed);
        sampling_.store(sampling > 0 ? sampling : 1, std::memory_order_relaxed);
        console_ = console_output;
        if (!filename.empty()) {
            if (file_.is_open()) {
                file_.close();
            }
            file_.open(filename, std::ios::app);
        }
        // 幂等启动后台写线程（仅首次调用 initialize 时创建）
        if (!writer_thread_.joinable()) {
            running_ = true;
            writer_thread_ = std::thread(&LoggerImpl::writer_loop, this);
        }
    }

    void shutdown() {
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            running_ = false;
        }
        cv_.notify_all();
        if (writer_thread_.joinable()) {
            writer_thread_.join();
        }
        // join 之后再关闭文件，确保写线程已排空并落盘
        std::lock_guard<std::mutex> lock(queue_mutex_);
        if (file_.is_open()) {
            file_.close();
        }
    }

    void set_file(const std::string& filename) {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        if (file_.is_open()) {
            file_.close();
        }
        file_.open(filename, std::ios::app);
    }

    // 运行时调整 INFO 级抽样分母（1=全量；N=每 N 条记 1 条；线程安全）
    void set_sampling(int sampling) {
        sampling_.store(sampling > 0 ? sampling : 1, std::memory_order_relaxed);
    }

    // 运行时调整最低记录级别（线程安全）
    void set_level(LogLevel level) {
        level_.store(static_cast<int>(level), std::memory_order_relaxed);
    }

    void write(LogLevel level, const std::string& message) {
        // 级别过滤（原子读，支持运行时 set_level 调整）
        if (static_cast<int>(level) < level_.load(std::memory_order_relaxed)) {
            return;
        }
        // INFO 级抽样：每 sampling_ 条记 1 条（WARN/ERROR 永不抽样，关键错误不漏）
        const int sampling = sampling_.load(std::memory_order_relaxed);
        if (level == LogLevel::INFO && sampling > 1) {
            const uint64_t n = info_counter_.fetch_add(1, std::memory_order_relaxed) + 1;
            if (n % static_cast<uint64_t>(sampling) != 0) {
                return;
            }
        }

        // 调用线程仅做格式化（无任何 I/O）：
        //   时间戳每秒缓存一次（cached_timestamp），行缓冲线程局部复用，
        //   避免每行 localtime_r(tz 解析) + put_time + ostringstream 分配。
        g_line.clear();
        g_line.reserve(256);
        g_line.append(cached_timestamp());
        g_line.append(" [");
        g_line.append(level_name(level));
        g_line.append("] ");
        g_line.append(message);
        g_line.push_back('\n');

        bool wake = false;
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            if (!running_) {
                return;  // 已关闭，丢弃该条日志
            }
            queue_.push_back(g_line);
            // 唤醒时机（避免每条日志都触发一次唤醒 + 上下文切换）：
            //   1) 队列由空变非空（首条日志）→ 启动凑批落盘
            //   2) 队列达到批量阈值 kBatchSize 的整数倍（缓冲区满）→ 立即落盘
            //      （写线程 wait_for 的满批谓词仅在唤醒时检查，若不在此唤醒，
            //       高负载下会睡满 500ms 才落盘，积压可达 500ms×QPS 行，故必须唤醒）
            wake = (queue_.size() == 1) || (queue_.size() % kBatchSize == 0);
        }
        if (wake) {
            cv_.notify_one();
        }
    }

private:
    static const char* level_name(LogLevel level) {
        switch (level) {
            case LogLevel::DEBUG: return "DEBUG";
            case LogLevel::INFO: return "INFO";
            case LogLevel::WARN: return "WARN";
            case LogLevel::ERROR: return "ERROR";
            default: return "UNKNOWN";
        }
    }

    // 后台写线程：批量取出队列并一次性落盘。
    // 采用"凑批"策略：收到首条日志后最多等待 kBatchWait 时间窗，
    // 期间累积多条日志一起写出 + 一次 flush，显著减少 syscall 与唤醒。
    void writer_loop() {
        // 凑批时间窗：每次批量落盘前最多等待 500ms 收集日志（≈2 次 flush/秒，
        // 相比逐条 flush 的 2 万次/秒 降低约 1 万倍）；
        // 若期间队列达到 kBatchSize（缓冲区满），write() 会唤醒写线程，
        // wait_for 满批谓词随之立即返回，立刻排空落盘，不受 500ms 时间窗限制。
        constexpr auto kBatchWait = std::chrono::milliseconds(500);
        // 整批合并缓冲的初始容量：跨批次复用，避免反复重新分配
        constexpr size_t kBatchBufInit = 16 * 1024;

        std::deque<std::string> batch;
        // 批量落盘缓冲：整批日志合并为单个大字符串，一次底层写入 + 一次 flush，
        // 相比逐条 file_ << line 减少 operator<< 的虚调用/缓冲管理开销，
        // 并使大块数据一次性到达 streambuf/内核缓冲（降低系统调用次数）。
        std::string out;
        out.reserve(kBatchBufInit);
        for (;;) {
            {
                std::unique_lock<std::mutex> lock(queue_mutex_);
                // 等队列非空或收到关闭信号
                cv_.wait(lock, [this] { return !queue_.empty() || !running_; });
                if (queue_.empty() && !running_) {
                    break;  // 已停止且队列排空
                }
                // 短暂等待凑批：期间新日志继续入队（不会反复唤醒写线程）
                cv_.wait_for(lock, kBatchWait, [this] {
                    return queue_.size() >= kBatchSize || !running_;
                });
                batch.swap(queue_);
            }

            // 批量落盘：整批合并为单个字符串（行已含 '\n'）
            out.clear();
            for (const auto& line : batch) {
                out += line;
            }
            batch.clear();

            // 一次底层写入（ostream::write → streambuf::xsputn 大块直写），
            // 文件与控制台各一次，避免逐条 << 的逐次调用
            if (!out.empty()) {
                if (file_.is_open()) {
                    file_.write(out.data(), static_cast<std::streamsize>(out.size()));
                }
                if (console_) {
                    std::cout.write(out.data(), static_cast<std::streamsize>(out.size()));
                }
            }

            // 每批一次性 flush：高负载下大幅减少 syscall，
            // 低负载下也能保证日志及时可见
            if (file_.is_open()) {
                file_.flush();
            }
            if (console_) {
                std::cout.flush();
            }
        }
    }

    std::ofstream file_;
    std::mutex queue_mutex_;
    std::condition_variable cv_;
    std::deque<std::string> queue_;
    std::thread writer_thread_;
    std::atomic<int> level_{static_cast<int>(LogLevel::INFO)};
    std::atomic<int> sampling_{1};           // INFO 级抽样分母（1=全量）
    std::atomic<uint64_t> info_counter_{0};  // INFO 级已记录计数（抽样用）
    // 缓冲区满阈值：队列达到该数量时唤醒写线程立即落盘，控制积压与内存占用
    static constexpr size_t kBatchSize = 1024;
    bool console_ = true;
    bool running_ = true;
};

static LoggerImpl& get_logger_impl() {
    static LoggerImpl impl;
    return impl;
}

Logger& Logger::instance() {
    static Logger logger;
    return logger;
}

Logger::Logger() = default;
Logger::~Logger() = default;

void Logger::initialize(LogLevel level, const std::string& filename, bool console_output,
                        int sampling) {
    get_logger_impl().initialize(level, filename, console_output, sampling);
}

void Logger::shutdown() {
    get_logger_impl().shutdown();
}

void Logger::set_file(const std::string& filename) {
    get_logger_impl().set_file(filename);
}

void Logger::set_sampling(int sampling) {
    get_logger_impl().set_sampling(sampling);
}

void Logger::set_level(LogLevel level) {
    get_logger_impl().set_level(level);
}

void Logger::debug(const char* file, int line, const std::string& message) {
    std::ostringstream msg;
    msg << file << ":" << line << " - " << message;
    get_logger_impl().write(LogLevel::DEBUG, msg.str());
}

void Logger::info(const std::string& message) {
    get_logger_impl().write(LogLevel::INFO, message);
}

void Logger::warn(const std::string& message) {
    get_logger_impl().write(LogLevel::WARN, message);
}

void Logger::error(const std::string& message) {
    get_logger_impl().write(LogLevel::ERROR, message);
}

}  // namespace infrastructure::common
