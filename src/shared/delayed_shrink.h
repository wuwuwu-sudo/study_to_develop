#pragma once

// ============================================================
// shared/delayed_shrink.h
// 延迟缩容策略（DelayedShrink）
//
// 背景：HttpServer::process_client 用 thread_local 复用 request/response 与
// 序列化缓冲（减少每请求分配）。复用的代价是缓冲会保留历史峰值容量
// （如一次大 POST body / 大响应后容量不回落），若不处理会长期占住多余内存。
//
// 本策略："延迟缩容"——缓冲只有在"持续小用达到阈值"时才判定应释放多余容量，
// 避免刚用大容量就立刻缩容导致的分配抖动；一旦再次用到大容量则重置窗口。
// 纯逻辑、无锁，线程内单对象使用，成本为每轮几次整数比较（可忽略）。
// ============================================================

#include <cstddef>
#include <string>

namespace shared {

class DelayedShrink {
public:
    // 每轮使用后调用。capacity/actual 用同一单位：字符串传字节；
    // 哈希表由便捷方法内部换算（桶数×sizeof(void*) 估字节）。
    // 返回 true 表示应释放多余容量。
    bool should_shrink(std::size_t capacity, std::size_t actual) noexcept {
        if (capacity >= kMinBig && actual <= capacity / kRatio) {
            if (++small_count_ >= kConsecutiveSmall) {
                small_count_ = 0;
                return true;
            }
        } else {
            small_count_ = 0;  // 又用到大容量/大实际量，重置延迟窗口
        }
        return false;
    }

    // 字符串缩容：判定通过后把容量收缩到与内容匹配（swap 保证真正释放），返回是否缩容
    bool shrink_if_needed(std::string& s) noexcept {
        if (should_shrink(s.capacity(), s.size())) {
            std::string small(s);  // 只拷贝实际内容（小）
            s.swap(small);         // 交换后 s 容量≈内容，大缓冲随 small 析构释放
            return true;
        }
        return false;
    }

    // 哈希表缩容：容量按 桶数×8 字节估算；释放全部桶与节点回到初始空态，返回是否缩容
    template <class Map>
    bool shrink_if_needed(Map& m) noexcept {
        if (should_shrink(m.bucket_count() * sizeof(void*), m.size())) {
            Map{}.swap(m);  // 交换出一个全新的空表，释放全部桶与节点
            return true;
        }
        return false;
    }

    void reset() noexcept { small_count_ = 0; }

private:
    // 容量达到 32KB 才值得缩（小缓冲保留无害）
    static constexpr std::size_t kMinBig = 32 * 1024;
    // 容量 ≥ 16× 实际使用量才算"多余容量"
    static constexpr std::size_t kRatio = 16;
    // 连续小用 1000 次才缩容（延迟窗口，防止抖动）
    static constexpr unsigned kConsecutiveSmall = 1000;
    unsigned small_count_ = 0;
};

}  // namespace shared
