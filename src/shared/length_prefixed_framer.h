#pragma once

#include <cstddef>
#include <string>

namespace shared {

// ============================================================
// LengthPrefixedFramer —— 长度前缀分帧协议（防 TCP 粘包/半包）
//
// 帧格式（4 字节大端长度前缀 + payload）:
//   +-----------+-------------+
//   | len (4B)  |   payload   |
//   +-----------+-------------+
//   len = payload 字节数（网络字节序，允许 0 = 空消息）
//
// 为什么能避免"收包丢包":
//   TCP 是字节流，一次 recv 可能只收到半个帧（半包），
//   也可能一次收到多个帧（粘包）。使用本分帧器的接收端应:
//     1. 把每次 recv 到的字节追加进累积缓冲区；
//     2. 循环调用 decode() 取帧，直到返回 0（半包，等更多数据）；
//     3. 每取到一帧（返回 1）即按 consumed 把已处理字节从缓冲删除，
//        剩余字节属于后续帧（粘包），不会丢也不会串。
//   长度前缀损坏/超限（超过 max_frame_size_）返回 -1，调用方应关闭连接，
//   防止恶意声明超大长度导致缓冲无限增长。
//
// 使用示例（长连接收包循环）:
//   conn.buffer += recv_bytes;
//   while (true) {
//       std::size_t consumed = 0;
//       std::string payload;
//       int rc = framer.decode(conn.buffer, consumed, payload);
//       if (rc == 0) break;                 // 半包，等更多数据
//       if (rc < 0) { close(conn); break; } // 非法帧，关闭连接
//       conn.buffer.erase(0, consumed);     // 清掉已消费帧，保留粘包剩余
//       handle_frame(payload);
//   }
// ============================================================
class LengthPrefixedFramer {
public:
    // 帧头固定 4 字节
    static constexpr std::size_t kPrefixSize = 4;
    // 默认最大帧长 64 MiB
    static constexpr std::size_t kDefaultMaxFrameSize = 64u * 1024u * 1024u;

    explicit LengthPrefixedFramer(
        std::size_t max_frame_size = kDefaultMaxFrameSize);

    // 编码：payload → 帧（4 字节长度前缀 + payload）。
    // payload 超限（> max_frame_size_ 或 4 字节长度无法表示）抛 std::invalid_argument。
    std::string encode(const std::string& payload) const;

    // 把编码后的帧追加到 out 末尾（批量组包发送，避免中间拷贝）。
    void append_frame(std::string& out, const std::string& payload) const;

    // 解码：从累积缓冲区 buffer 提取"一帧"。
    // 返回:
    //   1 = 取到一帧，consumed = 该帧字节数，out = payload（不含前缀）
    //   0 = 数据不完整（半包），需等待更多数据
    //  -1 = 协议非法（长度前缀损坏/超限），应关闭连接
    int decode(const std::string& buffer, std::size_t& consumed,
               std::string& out) const;

    std::size_t max_frame_size() const { return max_frame_size_; }

private:
    std::size_t max_frame_size_;
};

}  // namespace shared
