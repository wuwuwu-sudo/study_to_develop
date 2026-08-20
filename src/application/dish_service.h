#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "presentation/dto/dish_dto.h"
#include "infrastructure/repositories/interfaces/i_dish_repository.h"
#include "infrastructure/cache/multi_level_cache.h"
#include "application/read_result.h"
#include "shared/request_guard.h"

namespace application {

// 分页查询结果
struct DishPage {
    std::vector<DishDto> items;
    int total = 0;
    int page = 1;
    int page_size = 10;
    int total_pages = 1;
};

class DishService {
public:
    // 无缓存：直查仓储（原有行为，测试/低流量部署使用）
    explicit DishService(std::shared_ptr<infrastructure::repositories::IDishRepository> dish_repo);

    // 三级缓存：L1 本地 → L2 Redis → L3 仓储。
    // cache 传 nullptr 等价于无缓存构造。
    DishService(std::shared_ptr<infrastructure::repositories::IDishRepository> dish_repo,
                std::shared_ptr<infrastructure::cache::MultiLevelCache> cache,
                int local_ttl_seconds, int redis_ttl_seconds);

    // 带请求保护器（有界队列 + 熔断器 + 高水位降级）的构造。
    // guard 传 nullptr 等价于无保护（行为与四参构造一致）。
    DishService(std::shared_ptr<infrastructure::repositories::IDishRepository> dish_repo,
                std::shared_ptr<infrastructure::cache::MultiLevelCache> cache,
                int local_ttl_seconds, int redis_ttl_seconds,
                std::shared_ptr<shared::RequestGuard> guard);

    std::vector<DishDto> get_dishes(int merchant_id);
    DishPage get_dishes_paged(int merchant_id, int page, int page_size);
    // 分页结果字符串直出：缓存命中零解析（JSON 结构与响应体一致，供 handler 直接 set_json）。
    // 无缓存/异常时可能返回 nullopt（异常以 AppException 冒泡）。
    std::optional<std::string> get_dishes_paged_serialized(int merchant_id,
                                                           int page, int page_size);
    // 受保护读：经 RequestGuard 执行。
    //   kShed     → 高水位降级，仅查 L1 本地缓存（不查 Redis/DB、不做复杂业务），
    //               命中返回缓存；未命中返回最小空成功响应。
    //   kRejected → 熔断/队列超时，body 为空，调用方回简单错误页（不打 ERROR 日志）。
    SerializedReadResult get_dishes_paged_guarded(int merchant_id,
                                                  int page, int page_size);
    DishDto create_dish(const DishDto& dish);
    DishDto update_dish(const DishDto& dish);
    bool set_available(int dish_id, bool available);
    bool soft_delete(int dish_id);

private:
    // ---- 直查仓储（第三级数据源）----
    std::vector<DishDto> query_dishes(int merchant_id);
    DishPage query_dishes_paged(int merchant_id, int page, int page_size);

    // ---- 缓存加载（序列化）----
    std::optional<std::string> load_dishes_serialized(int merchant_id);
    std::optional<std::string> load_dishes_paged_serialized(int merchant_id,
                                                            int page, int page_size);

    // 写操作后按商家前缀失效 L1 + L2
    void invalidate_merchant(int merchant_id);

    std::shared_ptr<infrastructure::repositories::IDishRepository> dish_repo_;
    std::shared_ptr<infrastructure::cache::MultiLevelCache> cache_;
    std::shared_ptr<shared::RequestGuard> guard_;
    int local_ttl_seconds_ = 60;
    int redis_ttl_seconds_ = 300;
};

}  // namespace application
