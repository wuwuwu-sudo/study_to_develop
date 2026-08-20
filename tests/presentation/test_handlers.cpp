// ============================================================
// tests/presentation/test_handlers.cpp
// 对应:
//   - src/presentation/handlers/auth_handler.{h,cpp}
//   - src/presentation/handlers/dish_handler.{h,cpp}
//   - src/presentation/handlers/order_handler.{h,cpp}
// 注意：所有业务 handler 目前为占位实现（均返回 501 + TODO JSON）
// ============================================================
#include "test_framework.h"

#include <memory>

#include "application/auth_service.h"
#include "application/dish_service.h"
#include "application/order_service.h"
#include "infrastructure/session/session_manager.h"
#include "mocks/mock_repositories.h"
#include "presentation/handlers/auth_handler.h"
#include "presentation/handlers/dish_handler.h"
#include "presentation/handlers/order_handler.h"
#include "presentation/http/http_request.h"
#include "presentation/http/http_response.h"

using application::AuthService;
using application::DishService;
using application::OrderService;
using infrastructure::session::SessionManager;
using presentation::handlers::AuthHandler;
using presentation::handlers::DishHandler;
using presentation::handlers::OrderHandler;
using presentation::http::HttpRequest;
using presentation::http::HttpResponse;
using test_mocks::MockDishRepository;
using test_mocks::MockMerchantRepository;
using test_mocks::MockOrderRepository;
using test_mocks::MockUserRepository;

namespace {

// 组合所有服务与处理器，供各测试用例复用
struct HandlerFixture {
    std::shared_ptr<MockUserRepository> user_repo;
    std::shared_ptr<MockMerchantRepository> merchant_repo;
    std::shared_ptr<MockDishRepository> dish_repo;
    std::shared_ptr<MockOrderRepository> order_repo;
    SessionManager& sm;
    AuthService auth;
    DishService dish;
    OrderService order;
    AuthHandler auth_handler;
    DishHandler dish_handler;
    OrderHandler order_handler;

    HandlerFixture()
        : user_repo(std::make_shared<MockUserRepository>())
        , merchant_repo(std::make_shared<MockMerchantRepository>())
        , dish_repo(std::make_shared<MockDishRepository>())
        , order_repo(std::make_shared<MockOrderRepository>())
        , sm(SessionManager::instance())
        , auth(user_repo, merchant_repo, sm)
        , dish(dish_repo)
        , order(order_repo, dish_repo, user_repo, merchant_repo)
        , auth_handler(auth, sm)
        , dish_handler(dish, auth)
        , order_handler(order, auth, sm) {}
};

}  // namespace

// ---------------- AuthHandler ----------------

TEST(AuthHandler, IsUserLoggedInTrueWithValidSession) {
    HandlerFixture f;
    std::string sid = f.sm.create_session(1, 0);

    HttpRequest req;
    req.headers["session_id"] = sid;
    EXPECT_TRUE(f.auth_handler.is_user_logged_in(req));

    f.sm.destroy_session(sid);
}

TEST(AuthHandler, IsUserLoggedInFalseWithoutSession) {
    HandlerFixture f;
    HttpRequest req;
    EXPECT_FALSE(f.auth_handler.is_user_logged_in(req));
}

TEST(AuthHandler, IsMerchantLoggedInFalseWithoutSession) {
    HandlerFixture f;
    HttpRequest req;
    EXPECT_FALSE(f.auth_handler.is_merchant_logged_in(req));
}

TEST(AuthHandler, UserRegisterReturnsNotImplemented) {
    HandlerFixture f;
    HttpRequest req;
    HttpResponse resp;
    f.auth_handler.handle_user_register(req, resp);
    EXPECT_EQ(resp.status_code, 501);
    EXPECT_TRUE(resp.body.find("TODO") != std::string::npos);
}

TEST(AuthHandler, UserLoginReturnsNotImplemented) {
    HandlerFixture f;
    HttpRequest req;
    HttpResponse resp;
    f.auth_handler.handle_user_login(req, resp);
    EXPECT_EQ(resp.status_code, 501);
}

TEST(AuthHandler, UserInfoReturnsNotImplemented) {
    HandlerFixture f;
    HttpRequest req;
    HttpResponse resp;
    f.auth_handler.handle_user_info(req, resp);
    EXPECT_EQ(resp.status_code, 501);
}

TEST(AuthHandler, UserLogoutReturnsNotImplemented) {
    HandlerFixture f;
    HttpRequest req;
    HttpResponse resp;
    f.auth_handler.handle_user_logout(req, resp);
    EXPECT_EQ(resp.status_code, 501);
}

TEST(AuthHandler, MerchantLoginReturnsNotImplemented) {
    HandlerFixture f;
    HttpRequest req;
    HttpResponse resp;
    f.auth_handler.handle_merchant_login(req, resp);
    EXPECT_EQ(resp.status_code, 501);
}

TEST(AuthHandler, GetShopsReturnsNotImplemented) {
    HandlerFixture f;
    HttpRequest req;
    HttpResponse resp;
    f.auth_handler.handle_get_shops(req, resp);
    EXPECT_EQ(resp.status_code, 501);
}

// ---------------- DishHandler ----------------

TEST(DishHandler, GetDishesReturnsNotImplemented) {
    HandlerFixture f;
    HttpRequest req;
    HttpResponse resp;
    f.dish_handler.handle_get_dishes(req, resp);
    EXPECT_EQ(resp.status_code, 501);
}

TEST(DishHandler, AddDishReturnsNotImplemented) {
    HandlerFixture f;
    HttpRequest req;
    HttpResponse resp;
    f.dish_handler.handle_add_dish(req, resp);
    EXPECT_EQ(resp.status_code, 501);
    EXPECT_TRUE(resp.body.find("TODO") != std::string::npos);
}

TEST(DishHandler, EditDishReturnsNotImplemented) {
    HandlerFixture f;
    HttpRequest req;
    HttpResponse resp;
    f.dish_handler.handle_edit_dish(req, resp);
    EXPECT_EQ(resp.status_code, 501);
}

TEST(DishHandler, ToggleAvailableReturnsNotImplemented) {
    HandlerFixture f;
    HttpRequest req;
    HttpResponse resp;
    f.dish_handler.handle_toggle_available(req, resp);
    EXPECT_EQ(resp.status_code, 501);
}

TEST(DishHandler, DeleteDishReturnsNotImplemented) {
    HandlerFixture f;
    HttpRequest req;
    HttpResponse resp;
    f.dish_handler.handle_delete_dish(req, resp);
    EXPECT_EQ(resp.status_code, 501);
}

// ---------------- OrderHandler ----------------

TEST(OrderHandler, SubmitOrderReturnsNotImplemented) {
    HandlerFixture f;
    HttpRequest req;
    HttpResponse resp;
    f.order_handler.handle_order_submit(req, resp);
    EXPECT_EQ(resp.status_code, 501);
}

TEST(OrderHandler, UpdateStatusReturnsNotImplemented) {
    HandlerFixture f;
    HttpRequest req;
    HttpResponse resp;
    f.order_handler.handle_update_order_status(req, resp);
    EXPECT_EQ(resp.status_code, 501);
}

TEST(OrderHandler, MyOrdersReturnsNotImplemented) {
    HandlerFixture f;
    HttpRequest req;
    HttpResponse resp;
    f.order_handler.handle_my_orders(req, resp);
    EXPECT_EQ(resp.status_code, 501);
}

TEST(OrderHandler, MerchantOrdersReturnsNotImplemented) {
    HandlerFixture f;
    HttpRequest req;
    HttpResponse resp;
    f.order_handler.handle_merchant_orders(req, resp);
    EXPECT_EQ(resp.status_code, 501);
}
