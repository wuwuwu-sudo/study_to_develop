#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "presentation/dto/user_dto.h"
#include "infrastructure/cache/multi_level_cache.h"
#include "infrastructure/repositories/interfaces/i_user_repository.h"
#include "infrastructure/repositories/interfaces/i_merchant_repository.h"
#include "infrastructure/session/session_manager.h"
#include "application/read_result.h"
#include "shared/request_guard.h"

namespace application {

class AuthService {
public:
    AuthService(
        std::shared_ptr<infrastructure::repositories::IUserRepository> user_repo,
        std::shared_ptr<infrastructure::repositories::IMerchantRepository> merchant_repo,
        infrastructure::session::SessionManager& session_manager,
        std::shared_ptr<infrastructure::cache::MultiLevelCache> cache = nullptr,
        int local_ttl_seconds = 60,
        int redis_ttl_seconds = 300,
        std::shared_ptr<shared::RequestGuard> guard = nullptr
    );

    // 注册普通用户：输入验证 -> 查重 -> 密码加密 -> 保存用户。
    // 校验失败/用户名已存在/保存失败时抛出 infrastructure::common::AppException。
    UserDto register_user(const std::string& username, const std::string& password);

    // 用户登录：输入验证 -> 查找用户 -> 状态/密码校验 -> 创建会话。
    // 失败时抛出 AppException；成功返回会话 id。
    std::string login_user(const std::string& username, const std::string& password);

    // 登出：销毁会话（空会话幂等处理）。
    void logout_user(const std::string& session_id);

    // 校验会话是否有效（空会话直接返回 false）。
    bool validate_session(const std::string& session_id);

    // 商家注册：输入验证 -> 查重 -> 密码加密 -> 保存商家。
    // 校验失败/用户名已存在/保存失败时抛出 AppException；成功返回商家 id。
    int register_merchant(const std::string& username, const std::string& password,
                          const std::string& shop_name, const std::string& address);

    // 商家登录：验证密码后创建商家会话（user_id=0, merchant_id=商家id）；失败抛 AppException。
    std::string login_merchant(const std::string& username, const std::string& password);

    // 根据会话获取用户信息（未登录/会话无效/用户不存在时返回 nullopt）。
    std::optional<UserDto> get_user_info(const std::string& session_id);

    // 根据会话获取商家信息（未登录/会话无效/商家不存在时返回 nullopt）。
    std::optional<Merchant> get_merchant_info(const std::string& session_id);

    // 根据会话更新商家营业状态；未登录/商家不存在时返回 false。
    bool set_merchant_status(const std::string& session_id, bool open);

    // 查询所有营业中的商家（用于 /api/shops）。
    std::vector<Merchant> get_open_merchants();

    // 查询所有营业中的商家，返回序列化后的 JSON 数组字符串（走多级缓存；
    // 缓存命中时零序列化）。缓存不可用/未启用时直查并序列化。
    std::optional<std::string> get_open_merchants_serialized();

    // 受保护读：经 RequestGuard 执行。
    //   kShed     → 高水位降级，仅查 L1 本地缓存（不查 Redis/DB、不做复杂业务），
    //               命中返回缓存；未命中返回空数组（[]）。
    //   kRejected → 熔断/队列超时，body 为空，调用方回简单错误页（不打 ERROR 日志）。
    SerializedReadResult get_open_merchants_guarded();

private:
    // 商家开/关店、新商家注册等写操作后失效开业列表缓存
    void invalidate_open_merchants_cache();

    std::shared_ptr<infrastructure::repositories::IUserRepository> user_repo_;
    std::shared_ptr<infrastructure::repositories::IMerchantRepository> merchant_repo_;
    infrastructure::session::SessionManager& session_manager_;
    std::shared_ptr<infrastructure::cache::MultiLevelCache> cache_;
    std::shared_ptr<shared::RequestGuard> guard_;
    int local_ttl_seconds_;
    int redis_ttl_seconds_;
};

}  // namespace application
