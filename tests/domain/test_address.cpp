// ============================================================
// tests/domain/test_address.cpp
// 对应: src/domain/value_objects/address.{h,cpp}
// ============================================================
#include "test_framework.h"

#include "domain/value_objects/address.h"

TEST(Address, FullTextJoinsParts) {
    Address a("广东省", "深圳市", "南山区科技园路1号");
    EXPECT_STREQ(a.full_text(), "广东省 深圳市 南山区科技园路1号");
}

TEST(Address, FullTextWithEnglish) {
    Address a("Beijing", "Beijing", "Chaoyang District 100020");
    EXPECT_STREQ(a.full_text(), "Beijing Beijing Chaoyang District 100020");
}

TEST(Address, DefaultProducesSeparators) {
    Address a;
    // province_ + " " + city_ + " " + detail_，三个空串拼接得到两个空格
    EXPECT_STREQ(a.full_text(), "  ");
}
