#pragma once

#include <stdexcept>
#include <string>

namespace infrastructure::common {

class AppException : public std::runtime_error {
public:
    explicit AppException(const std::string& message);
};

// 乐观锁冲突：条件更新影响 0 行（期望旧值已被其他写并发修改）。
// handler 应映射为 HTTP 409。
class OptimisticLockException : public AppException {
public:
    explicit OptimisticLockException(const std::string& message);
};

class InfrastructureException : public std::runtime_error {
public:
    explicit InfrastructureException(const std::string& message, int code = 0);

    int error_code() const noexcept;

private:
    int code_ = 0;
};

class ConfigLoadException : public InfrastructureException {
public:
    explicit ConfigLoadException(const std::string& message);
};

}  // namespace infrastructure::common
