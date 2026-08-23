#include "application/auth_service.h"

#include <cstddef>
#include <optional>
#include <stdexcept>

#include <nlohmann/json.hpp>

#include "infrastructure/cache/local_cache.h"
#include "infrastructure/common/exception.h"
#include "infrastructure/common/logger.h"
#include "shared/utils.h"

namespace application {

using infrastructure::common::AppException;
using infrastructure::common::Logger;

namespace {

// 输入验证规则（防御性编程：显式、可读的常量）
constexpr std::size_t kMinUsernameLength = 3;
constexpr std::size_t kMaxUsernameLength = 32;
constexpr std::size_t kMinPasswordLength = 6;
constexpr std::size_t kMaxPasswordLength = 64;

// 校验注册/登录输入，返回错误描述；返回空串表示校验通过
std::string validate_credentials(const std::string& username, const std::string& password) {
    if (username.empty()) {
        return "用户名不能为空";
    }
    if (username.size() < kMinUsernameLength) {
        return "用户名长度不能少于 " + std::to_string(kMinUsernameLength) + " 个字符";
    }
    if (username.size() > kMaxUsernameLength) {
        return "用户名长度不能超过 " + std::to_string(kMaxUsernameLength) + " 个字符";
    }
    if (password.empty()) {
        return "密码不能为空";
    }
    if (password.size() < kMinPasswordLength) {
        return "密码长度不能少于 " + std::to_string(kMinPasswordLength) + " 个字符";
    }
    if (password.size() > kMaxPasswordLength) {
        return "密码长度不能超过 " + std::to_string(kMaxPasswordLength) + " 个字符";
    }
    return "";
}

// 开业商家列表的缓存键与失效前缀
constexpr char kOpenMerchantsCacheKey[] = "merchant:open:list";
constexpr char kOpenMerchantsCachePrefix[] = "merchant:open";

// 把商家列表序列化为 JSON 数组字符串（与 /api/shops 响应格式一致）
std::string serialize_open_merchants(const std::vector<Merchant>& shops) {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& m : shops) {
        arr.push_back({
            {"id", m.get_id()},
            {"username", m.get_username()},
            {"shop_name", m.get_shop_name()},
            {"address", m.get_address()}
        });
    }
    return arr.dump();
}

// 降级最小空响应：与 /api/shops 一致的空 JSON 数组（编译期常量，零构造开销）
const char kShedEmptyShopsResp[] = "[]";

}  // namespace

AuthService::AuthService(
    std::shared_ptr<infrastructure::repositories::IUserRepository> user_repo,
    std::shared_ptr<infrastructure::repositories::IMerchantRepository> merchant_repo,
    infrastructure::session::SessionManager& session_manager,
    std::shared_ptr<infrastructure::cache::MultiLevelCache> cache,
    int local_ttl_seconds,
    int redis_ttl_seconds,
    std::shared_ptr<shared::RequestGuard> guard
)
    : user_repo_(std::move(user_repo))
    , merchant_repo_(std::move(merchant_repo))
    , session_manager_(session_manager)
    , cache_(std::move(cache))
    , guard_(std::move(guard))
    , local_ttl_seconds_(local_ttl_seconds > 0 ? local_ttl_seconds : 60)
    , redis_ttl_seconds_(redis_ttl_seconds > 0 ? redis_ttl_seconds : 300) {}

UserDto AuthService::register_user(const std::string& username, const std::string& password) {
    // 1. 输入验证（防御性编程：先校验再处理）
    std::string error = validate_credentials(username, password);
    if (!error.empty()) {
        Logger::instance().warn("Register rejected: " + error);
        throw AppException(error);
    }

    // 2. 检查是否已有重复用户（先查后写，避免唯一约束冲突）
    std::optional<User> existing;
    try {
        existing = user_repo_->find_by_username(username);
    } catch (const std::exception& e) {
        Logger::instance().error("Register duplicate check failed: " + std::string(e.what()));
        throw AppException("检查用户失败，请稍后重试");
    }
    if (existing) {
        Logger::instance().warn("Register rejected: username already exists: " + username);
        throw AppException("用户名已存在: " + username);
    }

    // 3. 密码加密、构建用户实体并进行领域层兜底校验
    User user(0, username, hash_password(password), true);
    try {
        user.validate();
    } catch (const std::invalid_argument& e) {
        Logger::instance().warn("Register validation: " + std::string(e.what()));
        throw AppException(e.what());
    }

    // 4. 保存用户（对仓库异常与返回值做双重防护）
    int id = -1;
    try {
        id = user_repo_->save(user);
    } catch (const std::exception& e) {
        Logger::instance().error("Register save failed: " + std::string(e.what()));
        throw AppException("保存用户失败，请稍后重试");
    }
    if (id <= 0) {
        Logger::instance().error("Register save returned invalid id for: " + username);
        throw AppException("保存用户失败，请稍后重试");
    }

    Logger::instance().info("User registered: id=" + std::to_string(id) + ", username=" + username);

    // 5. 组装 DTO 返回
    UserDto dto;
    dto.id = id;
    dto.username = username;
    dto.role = UserRole::CUSTOMER;
    dto.active = true;
    return dto;
}

std::string AuthService::login_user(const std::string& username, const std::string& password) {
    // 1. 输入验证
    std::string error = validate_credentials(username, password);
    if (!error.empty()) {
        Logger::instance().warn("Login rejected: " + error);
        throw AppException(error);
    }

    // 2. 检查用户是否存在
    std::optional<User> user;
    try {
        user = user_repo_->find_by_username(username);
    } catch (const std::exception& e) {
        Logger::instance().error("Login lookup failed: " + std::string(e.what()));
        throw AppException("登录失败，请稍后重试");
    }
    if (!user) {
        // 防御性编程：不暴露“用户是否存在”，统一提示避免账号枚举
        Logger::instance().warn("Login rejected: user not found: " + username);
        throw AppException("用户名或密码错误");
    }

    // 3. 权限检查：账户状态（被禁用的账户不允许登录）
    if (!user->is_active()) {
        Logger::instance().warn("Login rejected: inactive user: " + username);
        throw AppException("账户已被禁用");
    }

    // 4. 密码验证
    if (!verify_password(password, user->get_password_hash())) {
        Logger::instance().warn("Login rejected: wrong password for: " + username);
        throw AppException("用户名或密码错误");
    }

    // 5. 会话管理：创建会话
    std::string session_id;
    try {
        session_id = session_manager_.create_session(user->get_id(), 0);
    } catch (const std::exception& e) {
        Logger::instance().error("Login session creation failed: " + std::string(e.what()));
        throw AppException("创建会话失败，请稍后重试");
    }
    if (session_id.empty()) {
        Logger::instance().error("Login session creation returned empty id: " + username);
        throw AppException("创建会话失败，请稍后重试");
    }

    Logger::instance().info("User logged in: id=" + std::to_string(user->get_id()) +
                            ", username=" + username);
    // [DIAG][会话] 以上为登录成功诊断日志：确认会话已创建
    return session_id;
}

void AuthService::logout_user(const std::string& session_id) {
    // 防御性编程：空会话视为已登出，幂等处理
    if (session_id.empty()) {
        Logger::instance().warn("Logout with empty session id");
        return;
    }
    session_manager_.destroy_session(session_id);
    Logger::instance().info("User logged out: session=" + session_id);
}

bool AuthService::validate_session(const std::string& session_id) {
    // 防御性编程：空会话直接判定无效
    if (session_id.empty()) {
        return false;
    }
    return session_manager_.validate_session(session_id);
}

int AuthService::register_merchant(const std::string& username, const std::string& password,
                                   const std::string& shop_name, const std::string& address) {
    // 1. 输入验证（防御性编程：先校验再处理）
    std::string error = validate_credentials(username, password);
    if (!error.empty()) {
        Logger::instance().warn("RegisterMerchant rejected: " + error);
        throw AppException(error);
    }
    if (shop_name.empty()) {
        Logger::instance().warn("RegisterMerchant rejected: empty shop_name");
        throw AppException("店铺名称不能为空");
    }
    if (address.empty()) {
        Logger::instance().warn("RegisterMerchant rejected: empty address");
        throw AppException("店铺地址不能为空");
    }

    // 2. 查重（先查后写，避免唯一约束冲突）
    std::optional<Merchant> existing;
    try {
        existing = merchant_repo_->find_by_username(username);
    } catch (const std::exception& e) {
        Logger::instance().error("RegisterMerchant dup check failed: " + std::string(e.what()));
        throw AppException("检查商家失败，请稍后重试");
    }
    if (existing) {
        Logger::instance().warn("RegisterMerchant rejected: username already exists: " + username);
        throw AppException("用户名已存在: " + username);
    }

    // 3. 密码加密、构建商家实体并进行领域层兜底校验
    Merchant merchant(0, username, shop_name, address, true);
    merchant.set_password_hash(hash_password(password));
    try {
        merchant.validate();
    } catch (const std::invalid_argument& e) {
        Logger::instance().warn("RegisterMerchant validation: " + std::string(e.what()));
        throw AppException(e.what());
    }

    // 4. 保存商家（对仓库异常与返回值做双重防护）
    int id = -1;
    try {
        id = merchant_repo_->save(merchant);
    } catch (const std::exception& e) {
        Logger::instance().error("RegisterMerchant save failed: " + std::string(e.what()));
        throw AppException("保存商家失败，请稍后重试");
    }
    if (id <= 0) {
        Logger::instance().error("RegisterMerchant save returned invalid id");
        throw AppException("保存商家失败，请稍后重试");
    }

    invalidate_open_merchants_cache();
    Logger::instance().info("Merchant registered: id=" + std::to_string(id) + ", username=" + username);
    return id;
}

std::string AuthService::login_merchant(const std::string& username, const std::string& password) {
    // 1. 输入验证
    std::string error = validate_credentials(username, password);
    if (!error.empty()) {
        Logger::instance().warn("LoginMerchant rejected: " + error);
        throw AppException(error);
    }

    // 2. 查找商家
    std::optional<Merchant> merchant;
    try {
        merchant = merchant_repo_->find_by_username(username);
    } catch (const std::exception& e) {
        Logger::instance().error("LoginMerchant lookup failed: " + std::string(e.what()));
        throw AppException("登录失败，请稍后重试");
    }
    if (!merchant) {
        // 防御性编程：统一提示，避免账号枚举
        Logger::instance().warn("LoginMerchant rejected: not found: " + username);
        throw AppException("用户名或密码错误");
    }

    // 3. 密码验证
    if (!verify_password(password, merchant->get_password_hash())) {
        Logger::instance().warn("LoginMerchant rejected: wrong password: " + username);
        throw AppException("用户名或密码错误");
    }

    // 4. 会话管理：创建商家会话（user_id=0, merchant_id=商家id）
    std::string session_id;
    try {
        session_id = session_manager_.create_session(0, merchant->get_id());
    } catch (const std::exception& e) {
        Logger::instance().error("LoginMerchant session creation failed: " + std::string(e.what()));
        throw AppException("创建会话失败，请稍后重试");
    }
    if (session_id.empty()) {
        Logger::instance().error("LoginMerchant session creation returned empty id");
        throw AppException("创建会话失败，请稍后重试");
    }

    Logger::instance().info("Merchant logged in: id=" + std::to_string(merchant->get_id()) +
                            ", username=" + username);
    return session_id;
}

std::optional<UserDto> AuthService::get_user_info(const std::string& session_id) {
    // ============================================================
    // [DIAG][会话] 登录校验诊断日志（排查“登录后立即被踢 / 会话丢失”）
    // 判读：
    //   empty session_id              -> 请求未携带会话（Cookie 未回传）
    //   session invalid/expired, sid= -> 会话在服务端失效（TTL/清理/存储问题）
    //   user not found, user_id=N     -> 会话有效但对应用户不存在（数据不一致）
    //   ok, user_id=N                 -> 校验通过
    // ============================================================
    if (session_id.empty()) {
        Logger::instance().warn("get_user_info: empty session_id");
        return std::nullopt;
    }
    auto info = session_manager_.find(session_id);
    if (!info || info->user_id <= 0) {
        Logger::instance().warn("get_user_info: session invalid/expired, sid=" +
                                session_id.substr(0, 8) + "...");
        return std::nullopt;
    }
    try {
        auto user = user_repo_->find_by_id(info->user_id);
        if (!user) {
            Logger::instance().warn("get_user_info: user not found, user_id=" +
                                    std::to_string(info->user_id));
            return std::nullopt;
        }
        Logger::instance().info("get_user_info: ok, user_id=" +
                                std::to_string(info->user_id));
        UserDto dto;
        dto.id = user->get_id();
        dto.username = user->get_username();
        dto.role = UserRole::CUSTOMER;
        dto.active = user->is_active();
        return dto;
    } catch (const std::exception& e) {
        Logger::instance().error("get_user_info failed: " + std::string(e.what()));
        return std::nullopt;
    }
}

std::optional<Merchant> AuthService::get_merchant_info(const std::string& session_id) {
    if (session_id.empty()) {
        return std::nullopt;
    }
    auto info = session_manager_.find(session_id);
    if (!info || info->merchant_id <= 0) {
        return std::nullopt;
    }
    try {
        return merchant_repo_->find_by_id(info->merchant_id);
    } catch (const std::exception& e) {
        Logger::instance().error("get_merchant_info failed: " + std::string(e.what()));
        return std::nullopt;
    }
}

bool AuthService::set_merchant_status(const std::string& session_id, bool open) {
    auto merchant = get_merchant_info(session_id);
    if (!merchant) {
        Logger::instance().warn("set_merchant_status: invalid session or merchant");
        return false;
    }
    merchant->set_open(open);
    try {
        // 幂等条件更新营业状态（仅当状态变化才写），避免整行覆盖并发修改
        bool ok = merchant_repo_->update_open_status(merchant->get_id(), open);
        if (ok) {
            invalidate_open_merchants_cache();
            Logger::instance().info("Merchant status updated: id=" +
                                    std::to_string(merchant->get_id()) +
                                    ", open=" + (open ? "true" : "false"));
        }
        return ok;
    } catch (const std::exception& e) {
        Logger::instance().error("set_merchant_status failed: " + std::string(e.what()));
        return false;
    }
}

std::vector<Merchant> AuthService::get_open_merchants() {
    try {
        return merchant_repo_->find_open_merchants();
    } catch (const std::exception& e) {
        Logger::instance().error("get_open_merchants failed: " + std::string(e.what()));
        throw AppException("查询商家失败，请稍后重试");
    }
}

std::optional<std::string> AuthService::get_open_merchants_serialized() {
    if (!cache_) {
        // 未启用缓存：直查并序列化（行为与旧版一致）
        return serialize_open_merchants(get_open_merchants());
    }
    // 走三级缓存：L1 本地 → L2 Redis → L3 数据源；命中即返回缓存 JSON
    return cache_->get(
        kOpenMerchantsCacheKey,
        [this] { return serialize_open_merchants(get_open_merchants()); },
        local_ttl_seconds_, redis_ttl_seconds_);
}

SerializedReadResult AuthService::get_open_merchants_guarded() {
    SerializedReadResult result;
    // 无保护器：行为与 get_open_merchants_serialized 完全一致
    if (!guard_) {
        result.body = get_open_merchants_serialized();
        return result;
    }
    result.status = guard_->execute(
        [&]() -> bool {
            // 正常路径：完整三级缓存（L1 → L2 → L3 数据源），结果回填缓存
            if (!cache_) {
                auto body = serialize_open_merchants(get_open_merchants());
                result.body = std::move(body);
                return true;
            }
            auto body = cache_->get(
                kOpenMerchantsCacheKey,
                [&] { return serialize_open_merchants(get_open_merchants()); },
                local_ttl_seconds_, redis_ttl_seconds_);
            if (body) {
                result.body = std::move(body);
                return true;
            }
            return false;
        },
        [&]() {
            // 高水位快速降级：仅查 L1 本地内存缓存，不查 Redis/DB、不做复杂业务
            if (cache_) {
                if (auto v = cache_->local()->get(kOpenMerchantsCacheKey)) {
                    result.body = std::move(v);
                    return;
                }
            }
            // L1 未命中：返回空数组（[]），成本极低
            result.body = kShedEmptyShopsResp;
        });
    return result;
}

void AuthService::invalidate_open_merchants_cache() {
    if (cache_) {
        cache_->clear_prefix(kOpenMerchantsCachePrefix);
        Logger::instance().info("Open merchants cache invalidated");
    }
}

}  // namespace application
