#include "infrastructure/common/exception.h"


//抛出异常信息
namespace infrastructure::common {

AppException::AppException(const std::string& message)
    : std::runtime_error(message) {}

OptimisticLockException::OptimisticLockException(const std::string& message)
    : AppException(message) {}

InfrastructureException::InfrastructureException(const std::string& message, int code)
    : std::runtime_error(message)
    , code_(code) {}

int InfrastructureException::error_code() const noexcept {
    return code_;
}

ConfigLoadException::ConfigLoadException(const std::string& message)
    : InfrastructureException(message, 1) {}

}  // namespace infrastructure::common
