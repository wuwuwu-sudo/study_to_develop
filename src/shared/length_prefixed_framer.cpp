#include "shared/length_prefixed_framer.h"

#include <cstdint>
#include <stdexcept>

namespace shared {

namespace {

// 把 32 位长度写为 4 字节大端（写入 out 末尾）
void write_be32(std::string& out, std::uint32_t value) {
    out.push_back(static_cast<char>((value >> 24) & 0xFFu));
    out.push_back(static_cast<char>((value >> 16) & 0xFFu));
    out.push_back(static_cast<char>((value >> 8) & 0xFFu));
    out.push_back(static_cast<char>(value & 0xFFu));
}

// 从 4 字节大端读 32 位长度；不足 4 字节返回 false（半包）
bool read_be32(const std::string& buffer, std::uint32_t& value) {
    if (buffer.size() < LengthPrefixedFramer::kPrefixSize) {
        return false;
    }
    const auto byte = [&buffer](std::size_t i) {
        return static_cast<std::uint32_t>(
            static_cast<unsigned char>(buffer[i]));
    };
    value = (byte(0) << 24) | (byte(1) << 16) | (byte(2) << 8) | byte(3);
    return true;
}

}  // namespace

LengthPrefixedFramer::LengthPrefixedFramer(std::size_t max_frame_size)
    : max_frame_size_(max_frame_size) {}

std::string LengthPrefixedFramer::encode(const std::string& payload) const {
    if (payload.size() > max_frame_size_) {
        throw std::invalid_argument(
            "LengthPrefixedFramer::encode: payload exceeds max_frame_size");
    }
    if (payload.size() > static_cast<std::size_t>(0xFFFFFFFFu)) {
        throw std::invalid_argument(
            "LengthPrefixedFramer::encode: payload too large for 4-byte length");
    }
    std::string frame;
    frame.reserve(kPrefixSize + payload.size());
    write_be32(frame, static_cast<std::uint32_t>(payload.size()));
    frame.append(payload);
    return frame;
}

void LengthPrefixedFramer::append_frame(std::string& out,
                                        const std::string& payload) const {
    if (payload.size() > max_frame_size_) {
        throw std::invalid_argument(
            "LengthPrefixedFramer::append_frame: payload exceeds max_frame_size");
    }
    if (payload.size() > static_cast<std::size_t>(0xFFFFFFFFu)) {
        throw std::invalid_argument(
            "LengthPrefixedFramer::append_frame: payload too large for 4-byte length");
    }
    write_be32(out, static_cast<std::uint32_t>(payload.size()));
    out.append(payload);
}

int LengthPrefixedFramer::decode(const std::string& buffer,
                                 std::size_t& consumed,
                                 std::string& out) const {
    consumed = 0;

    // 1. 前缀未收全（半包）
    std::uint32_t len = 0;
    if (!read_be32(buffer, len)) {
        return 0;
    }

    // 2. 长度前缀损坏/超限：恶意声明超大长度会让缓冲无限增长，直接拒绝
    if (static_cast<std::size_t>(len) > max_frame_size_) {
        return -1;
    }

    // 3. payload 未收全（半包）：已收到前缀但正文不完整
    if (buffer.size() < kPrefixSize + static_cast<std::size_t>(len)) {
        return 0;
    }

    // 4. 取整帧；剩余字节（粘包）留给调用方继续 decode
    out = buffer.substr(kPrefixSize, static_cast<std::size_t>(len));
    consumed = kPrefixSize + static_cast<std::size_t>(len);
    return 1;
}

}  // namespace shared
