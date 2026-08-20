// ============================================================
// tests/application/test_auth_service.cpp
// 对应: src/application/auth_service.{h,cpp}
// 注意：
//   - register_user() 目前为占位实现（忽略密码）
//   - login_user() 目前恒返回 "session_id_placeholder"
// ============================================================
#include "test_framework.h"

#include <memory>

#include "application/auth_service.h"
#include "infrastructure/session/session_manager.h"
#include "mocks/mock_repositories.h"

using application::AuthService;
using infrastructure::session::SessionManager;
using test_mocks::MockMerchantRepository;
using test_mocks::MockUserRepository;

namespace {

struct AuthFixture {
    std::shared_ptr<MockUserRepository> user_repo;
    std::shared_ptr<MockMerchantRepository> merchant_repo;
    SessionManager& sm;
    AuthService service;

    AuthFixture()
        : user_repo(std::make_shared<MockUserRepository>())
        , merchant_repo(std::make_shared<MockMerchantRepository>())
        , sm(SessionManager::instance())
        , service(user_repo, merchant_repo, sm) {}
};

}  // namespace

TEST(AuthService, RegisterUserReturnsDto) {
    AuthFixture f;
    UserDto dto = f.service.register_user("alice", "secret_password");
    EXPECT_STREQ(dto.username, "alice");
    EXPECT_EQ(static_cast<int>(dto.role), static_cast<int>(UserRole::CUSTOMER));
    EXPECT_TRUE(dto.active);
}

TEST(AuthService, RegisterUserKeepsUsernameIdentity) {
    AuthFixture f;
    UserDto dto = f.service.register_user("bob_商家", "pw");
    EXPECT_STREQ(dto.username, "bob_商家");
}

TEST(AuthService, LoginReturnsPlaceholderSession) {
    AuthFixture f;
    // 占位实现：目前不真正校验密码，也不创建会话
    std::string session = f.service.login_user("alice", "any_password");
    EXPECT_STREQ(session, "session_id_placeholder");
}

TEST(AuthService, ValidateSessionReflectsSessionManager) {
    AuthFixture f;
    // 手动创建会话，然后通过服务验证
    std::string sid = f.sm.create_session(1, 0);
    EXPECT_TRUE(f.service.validate_session(sid));
    f.sm.destroy_session(sid);
    EXPECT_FALSE(f.service.validate_session(sid));
}

TEST(AuthService, ValidateSessionUnknownIsFalse) {
    AuthFixture f;
    EXPECT_FALSE(f.service.validate_session("no_such_session"));
}

TEST(AuthService, LogoutDestroysSession) {
    AuthFixture f;
    std::string sid = f.sm.create_session(1, 0);
    EXPECT_TRUE(f.service.validate_session(sid));

    f.service.logout_user(sid);
    EXPECT_FALSE(f.service.validate_session(sid));
}
