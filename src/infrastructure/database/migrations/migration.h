#pragma once

#include "infrastructure/database/db_manager.h"

namespace infrastructure::database {

class Migration {
public:
    virtual ~Migration() = default;

    virtual void up(DbManager& db) = 0;
    virtual void down(DbManager& db) = 0;
};

}  // namespace infrastructure::database

//数据库迁移，等需要的时候再实现cpp文件