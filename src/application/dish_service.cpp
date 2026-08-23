#include "application/dish_service.h"

#include <nlohmann/json.hpp>

#include <optional>
#include <stdexcept>
#include <utility>

#include "domain/models/dish.h"
#include "domain/value_objects/money.h"
#include "infrastructure/cache/local_cache.h"
#include "infrastructure/common/exception.h"
#include "infrastructure/common/logger.h"
#include "shared/request_guard.h"

namespace application {

using infrastructure::common::AppException;
using infrastructure::common::Logger;
using infrastructure::common::OptimisticLockException;

namespace {

constexpr int kMinMerchantId = 1;
constexpr double kMinPrice = 0.0;

DishDto to_dto(const Dish& dish) {
    DishDto dto;
    dto.id = dish.get_id();
    dto.merchant_id = dish.get_merchant_id();
    dto.name = dish.get_name();
    dto.price = dish.get_price().get_yuan();
    dto.category = dish.get_category();
    dto.description = dish.get_description();
    dto.available = dish.is_available();
    return dto;
}

// 调用领域层校验并将 std::invalid_argument 转换为 AppException
void validate_entity(const Dish& dish) {
    try {
        dish.validate();
    } catch (const std::invalid_argument& e) {
        throw AppException(e.what());
    }
}

// ---- 多级缓存：key 生成与 JSON 序列化 ----
std::string dish_list_key(int merchant_id) {
    return "dish:merchant:" + std::to_string(merchant_id) + ":list";
}

std::string dish_paged_key(int merchant_id, int page, int page_size) {
    return "dish:merchant:" + std::to_string(merchant_id) + ":paged:" +
           std::to_string(page) + ":" + std::to_string(page_size);
}

// 响应体直出缓存 key（与对象型分页缓存隔离，结构与 handler 响应体一致）
std::string dish_paged_resp_key(int merchant_id, int page, int page_size) {
    return "dish:merchant:" + std::to_string(merchant_id) + ":paged:" +
           std::to_string(page) + ":" + std::to_string(page_size) + ":resp";
}

nlohmann::json to_json(const DishDto& d) {
    return nlohmann::json{{"id", d.id},
                          {"merchant_id", d.merchant_id},
                          {"name", d.name},
                          {"price", d.price},
                          {"category", d.category},
                          {"description", d.description},
                          {"available", d.available}};
}

DishDto dish_from_json(const nlohmann::json& j) {
    DishDto d;
    d.id = j.value("id", 0);
    d.merchant_id = j.value("merchant_id", 0);
    d.name = j.value("name", std::string());
    d.price = j.value("price", 0.0);
    d.category = j.value("category", std::string());
    d.description = j.value("description", std::string());
    d.available = j.value("available", true);
    return d;
}

// 反序列化失败返回 nullopt（视为脏缓存，由调用方清除后直查）
std::optional<std::vector<DishDto>> deserialize_dishes(const std::string& raw) {
    try {
        auto arr = nlohmann::json::parse(raw);
        if (!arr.is_array()) {
            return std::nullopt;
        }
        std::vector<DishDto> result;
        for (const auto& j : arr) {
            if (!j.is_object()) {
                return std::nullopt;
            }
            result.push_back(dish_from_json(j));
        }
        return result;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::optional<DishPage> deserialize_dish_page(const std::string& raw) {
    try {
        auto obj = nlohmann::json::parse(raw);
        if (!obj.is_object()) {
            return std::nullopt;
        }
        DishPage page;
        page.page = obj.value("page", 1);
        page.page_size = obj.value("page_size", 10);
        page.total = obj.value("total", 0);
        page.total_pages = obj.value("total_pages", 1);
        auto& items = obj["items"];
        if (!items.is_array()) {
            return std::nullopt;
        }
        for (const auto& j : items) {
            if (!j.is_object()) {
                return std::nullopt;
            }
            page.items.push_back(dish_from_json(j));
        }
        return page;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

// 构造与 handler 响应体一致的 JSON 字符串（success + dishes + 分页元数据）
std::string serialize_page_response(const DishPage& page_result) {
    nlohmann::json obj;
    obj["success"] = true;
    obj["page"] = page_result.page;
    obj["page_size"] = page_result.page_size;
    obj["total"] = page_result.total;
    obj["total_pages"] = page_result.total_pages;
    obj["dishes"] = nlohmann::json::array();
    for (const auto& d : page_result.items) {
        obj["dishes"].push_back(to_json(d));
    }
    return obj.dump();
}

// 降级最小空成功响应：与 handler 响应体结构一致，仅含空列表与分页元数据，
// 极轻量（编译期常量字符串，无 JSON 解析/构造开销）。
const char kShedEmptyDishesResp[] =
    "{\"success\":true,\"page\":1,\"page_size\":10,\"total\":0,\"total_pages\":1,\"dishes\":[]}";

}  // namespace

DishService::DishService(std::shared_ptr<infrastructure::repositories::IDishRepository> dish_repo)
    : dish_repo_(std::move(dish_repo)) {}

DishService::DishService(
    std::shared_ptr<infrastructure::repositories::IDishRepository> dish_repo,
    std::shared_ptr<infrastructure::cache::MultiLevelCache> cache,
    int local_ttl_seconds, int redis_ttl_seconds)
    : DishService(std::move(dish_repo), std::move(cache), local_ttl_seconds,
                  redis_ttl_seconds, nullptr) {}

DishService::DishService(
    std::shared_ptr<infrastructure::repositories::IDishRepository> dish_repo,
    std::shared_ptr<infrastructure::cache::MultiLevelCache> cache,
    int local_ttl_seconds, int redis_ttl_seconds,
    std::shared_ptr<shared::RequestGuard> guard)
    : dish_repo_(std::move(dish_repo))
    , cache_(std::move(cache))
    , guard_(std::move(guard))
    , local_ttl_seconds_(local_ttl_seconds > 0 ? local_ttl_seconds : 60)
    , redis_ttl_seconds_(redis_ttl_seconds > 0 ? redis_ttl_seconds : 300) {}

std::vector<DishDto> DishService::get_dishes(int merchant_id) {
    // 防御性编程：商家 ID 非法时直接返回空列表
    if (merchant_id < kMinMerchantId) {
        Logger::instance().warn("get_dishes: invalid merchant_id=" + std::to_string(merchant_id));
        return {};
    }
    if (!cache_) {
        return query_dishes(merchant_id);
    }

    const std::string key = dish_list_key(merchant_id);
    auto cached = cache_->get(
        key,
        [this, merchant_id] { return load_dishes_serialized(merchant_id); },
        local_ttl_seconds_, redis_ttl_seconds_);
    if (cached) {
        auto parsed = deserialize_dishes(*cached);
        if (parsed) {
            return std::move(*parsed);
        }
        // 缓存数据损坏：清除后直查，避免返回脏数据
        cache_->del(key);
    }
    return query_dishes(merchant_id);
}

std::vector<DishDto> DishService::query_dishes(int merchant_id) {
    std::vector<DishDto> result;
    try {
        for (const auto& dish : dish_repo_->find_by_merchant(merchant_id)) {
            result.push_back(to_dto(dish));
        }
    } catch (const std::exception& e) {
        Logger::instance().error("get_dishes failed: " + std::string(e.what()));
        throw AppException("查询菜品失败，请稍后重试");
    }
    return result;
}

DishPage DishService::get_dishes_paged(int merchant_id, int page, int page_size) {
    // 防御性编程：商家 ID 非法时直接返回空分页结果
    if (merchant_id < kMinMerchantId) {
        Logger::instance().warn("get_dishes_paged: invalid merchant_id=" + std::to_string(merchant_id));
        return DishPage{};
    }

    // 防御性编程：页码与页大小收敛到合法范围
    if (page < 1) {
        page = 1;
    }
    if (page_size < 1) {
        page_size = 10;
    }

    if (!cache_) {
        return query_dishes_paged(merchant_id, page, page_size);
    }

    const std::string key = dish_paged_key(merchant_id, page, page_size);
    auto cached = cache_->get(
        key,
        [this, merchant_id, page, page_size] {
            return load_dishes_paged_serialized(merchant_id, page, page_size);
        },
        local_ttl_seconds_, redis_ttl_seconds_);
    if (cached) {
        auto parsed = deserialize_dish_page(*cached);
        if (parsed) {
            return std::move(*parsed);
        }
        // 缓存数据损坏：清除后直查，避免返回脏数据
        cache_->del(key);
    }
    return query_dishes_paged(merchant_id, page, page_size);
}

std::optional<std::string> DishService::get_dishes_paged_serialized(int merchant_id,
                                                                    int page,
                                                                    int page_size) {
    // 防御性编程：商家 ID 非法时返回空页（与 get_dishes_paged 语义一致）
    if (merchant_id < kMinMerchantId) {
        Logger::instance().warn("get_dishes_paged_serialized: invalid merchant_id=" +
                                std::to_string(merchant_id));
        return serialize_page_response(DishPage{});
    }

    // 防御性编程：页码与页大小收敛到合法范围
    if (page < 1) {
        page = 1;
    }
    if (page_size < 1) {
        page_size = 10;
    }

    if (!cache_) {
        return serialize_page_response(query_dishes_paged(merchant_id, page, page_size));
    }

    const std::string key = dish_paged_resp_key(merchant_id, page, page_size);
    return cache_->get(
        key,
        [this, merchant_id, page, page_size] {
            return serialize_page_response(query_dishes_paged(merchant_id, page, page_size));
        },
        local_ttl_seconds_, redis_ttl_seconds_);
}

SerializedReadResult DishService::get_dishes_paged_guarded(int merchant_id,
                                                           int page,
                                                           int page_size) {
    SerializedReadResult result;
    // 无保护器：行为与 get_dishes_paged_serialized 完全一致
    if (!guard_) {
        result.body = get_dishes_paged_serialized(merchant_id, page, page_size);
        return result;
    }

    // 防御性编程：商家 ID 非法时返回空页（与 get_dishes_paged 语义一致）
    if (merchant_id < kMinMerchantId) {
        result.body = serialize_page_response(DishPage{});
        return result;
    }
    // 页码与页大小收敛到合法范围
    if (page < 1) {
        page = 1;
    }
    if (page_size < 1) {
        page_size = 10;
    }

    const std::string key = dish_paged_resp_key(merchant_id, page, page_size);
    result.status = guard_->execute(
        [&]() -> bool {
            // 正常路径：完整三级缓存（L1 → L2 → L3 仓储），结果回填缓存
            if (!cache_) {
                auto body = serialize_page_response(
                    query_dishes_paged(merchant_id, page, page_size));
                result.body = std::move(body);
                return true;
            }
            auto body = cache_->get(
                key,
                [&] {
                    return serialize_page_response(
                        query_dishes_paged(merchant_id, page, page_size));
                },
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
                if (auto v = cache_->local()->get(key)) {
                    result.body = std::move(v);
                    return;
                }
            }
            // L1 未命中：返回最小空成功响应（成功 + 空列表），成本极低
            result.body = kShedEmptyDishesResp;
        });
    return result;
}

DishPage DishService::query_dishes_paged(int merchant_id, int page, int page_size) {
    DishPage result;
    try {
        result.page = page;
        result.page_size = page_size;
        result.total = dish_repo_->count_by_merchant(merchant_id);
        result.total_pages = result.total == 0 ? 1 : (result.total + page_size - 1) / page_size;
        if (result.total_pages < 1) {
            result.total_pages = 1;
        }
        if (result.page > result.total_pages) {
            result.page = result.total_pages;
        }

        for (const auto& dish : dish_repo_->find_paged(merchant_id, result.page, page_size)) {
            result.items.push_back(to_dto(dish));
        }
    } catch (const std::exception& e) {
        Logger::instance().error("get_dishes_paged failed: " + std::string(e.what()));
        throw AppException("查询菜品失败，请稍后重试");
    }
    return result;
}

std::optional<std::string> DishService::load_dishes_serialized(int merchant_id) {
    std::vector<DishDto> dishes = query_dishes(merchant_id);
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& d : dishes) {
        arr.push_back(to_json(d));
    }
    return arr.dump();
}

std::optional<std::string> DishService::load_dishes_paged_serialized(int merchant_id,
                                                                     int page,
                                                                     int page_size) {
    DishPage page_result = query_dishes_paged(merchant_id, page, page_size);
    nlohmann::json obj;
    obj["page"] = page_result.page;
    obj["page_size"] = page_result.page_size;
    obj["total"] = page_result.total;
    obj["total_pages"] = page_result.total_pages;
    obj["items"] = nlohmann::json::array();
    for (const auto& d : page_result.items) {
        obj["items"].push_back(to_json(d));
    }
    return obj.dump();
}

void DishService::invalidate_merchant(int merchant_id) {
    if (!cache_) {
        return;
    }
    const std::string prefix = "dish:merchant:" + std::to_string(merchant_id) + ":";
    cache_->clear_prefix(prefix);
    LOG_DEBUG("Dish cache invalidated: merchant=" + std::to_string(merchant_id));
}

DishDto DishService::create_dish(const DishDto& dish) {
    // 防御性编程：输入验证（先校验再处理）
    if (dish.merchant_id < kMinMerchantId) {
        Logger::instance().warn("create_dish: invalid merchant_id=" + std::to_string(dish.merchant_id));
        throw AppException("商家ID无效");
    }

    Dish entity(dish.merchant_id, dish.name, Money(dish.price), dish.category, dish.description);
    if (!dish.available) {
        entity.set_available(false);
    }

    // 领域层兜底校验（名称非空、价格非负）
    validate_entity(entity);

    int new_id = -1;
    try {
        new_id = dish_repo_->save(entity);
    } catch (const std::exception& e) {
        Logger::instance().error("create_dish save failed: " + std::string(e.what()));
        throw AppException("保存菜品失败，请稍后重试");
    }
    if (new_id <= 0) {
        Logger::instance().error("create_dish: save returned invalid id");
        throw AppException("保存菜品失败，请稍后重试");
    }

    // 写操作后失效该商家全部缓存（L1 + L2）
    invalidate_merchant(dish.merchant_id);

    Logger::instance().info("Dish created: id=" + std::to_string(new_id) +
                            ", merchant=" + std::to_string(dish.merchant_id));
    DishDto result = dish;
    result.id = new_id;
    return result;
}

DishDto DishService::update_dish(const DishDto& dish) {
    // 防御性编程：ID 校验
    if (dish.id <= 0) {
        Logger::instance().warn("update_dish: invalid id=" + std::to_string(dish.id));
        throw AppException("菜品ID无效");
    }

    // 查询现有菜品；不存在则明确报错（不再静默返回原样）
    std::optional<Dish> existing;
    try {
        existing = dish_repo_->find_by_id(dish.id);
    } catch (const std::exception& e) {
        Logger::instance().error("update_dish find failed: " + std::string(e.what()));
        throw AppException("查询菜品失败，请稍后重试");
    }
    if (!existing) {
        Logger::instance().warn("update_dish: dish not found, id=" + std::to_string(dish.id));
        throw AppException("菜品不存在");
    }

    // 保留原有 merchant_id，避免越权修改归属商家；并保留乐观锁版本号
    Dish updated(dish.id, existing->get_merchant_id(),
                 dish.name, Money(dish.price), dish.category, dish.description);
    if (!dish.available) {
        updated.set_available(false);
    }
    updated.set_version(existing->get_version());

    // 领域层兜底校验（名称非空、价格非负）
    validate_entity(updated);

    // 乐观锁版本列条件更新：仅当当前 version == 读取时的版本才更新；0 行 = 并发冲突
    try {
        int rc = dish_repo_->update_optimistic(updated, existing->get_version());
        if (rc == 0) {
            throw OptimisticLockException("菜品已被其他操作修改，请刷新后重试");
        }
        if (rc != 1) {
            throw AppException("更新菜品失败");
        }
    } catch (const OptimisticLockException&) {
        throw;  // 透传，由 handler 映射 409
    } catch (const std::exception& e) {
        Logger::instance().error("update_dish failed: " + std::string(e.what()));
        throw AppException("更新菜品失败，请稍后重试");
    }

    // 写操作后失效该商家全部缓存（L1 + L2）
    invalidate_merchant(existing->get_merchant_id());

    Logger::instance().info("Dish updated: id=" + std::to_string(dish.id));
    return to_dto(updated);
}

bool DishService::set_available(int dish_id, bool available) {
    if (dish_id <= 0) {
        Logger::instance().warn("set_available: invalid dish_id=" + std::to_string(dish_id));
        return false;
    }
    try {
        // 先查出归属商家，用于写操作后的缓存失效
        std::optional<Dish> existing = dish_repo_->find_by_id(dish_id);
        bool ok = dish_repo_->set_available(dish_id, available);
        if (ok) {
            if (existing) {
                invalidate_merchant(existing->get_merchant_id());
            }
            Logger::instance().info("Dish availability updated: id=" + std::to_string(dish_id) +
                                    ", available=" + (available ? "true" : "false"));
        } else {
            Logger::instance().warn("set_available: dish not found or failed, id=" +
                                    std::to_string(dish_id));
        }
        return ok;
    } catch (const std::exception& e) {
        Logger::instance().error("set_available failed: " + std::string(e.what()));
        return false;
    }
}

bool DishService::soft_delete(int dish_id) {
    if (dish_id <= 0) {
        Logger::instance().warn("soft_delete: invalid dish_id=" + std::to_string(dish_id));
        return false;
    }
    try {
        // 先查出归属商家，用于写操作后的缓存失效
        std::optional<Dish> existing = dish_repo_->find_by_id(dish_id);
        bool ok = dish_repo_->delete_dish(dish_id);
        if (ok) {
            if (existing) {
                invalidate_merchant(existing->get_merchant_id());
            }
            Logger::instance().info("Dish soft-deleted: id=" + std::to_string(dish_id));
        } else {
            Logger::instance().warn("soft_delete: dish not found or already deleted, id=" +
                                    std::to_string(dish_id));
        }
        return ok;
    } catch (const std::exception& e) {
        Logger::instance().error("soft_delete failed: " + std::string(e.what()));
        return false;
    }
}

}  // namespace application
