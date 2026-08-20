// ============================================================
// tests/domain/test_money.cpp
// 对应: src/domain/value_objects/money.{h,cpp}
// ============================================================
#include "test_framework.h"

#include "domain/value_objects/money.h"

TEST(Money, DefaultIsZero) {
    Money m;
    EXPECT_NEAR(m.get_yuan(), 0.0, 1e-9);
    EXPECT_FALSE(m.is_negative());
}

TEST(Money, ConstructWithValue) {
    Money m(12.34);
    EXPECT_NEAR(m.get_yuan(), 12.34, 1e-9);
    EXPECT_FALSE(m.is_negative());
}

TEST(Money, NegativeDetected) {
    Money m(-5.0);
    EXPECT_TRUE(m.is_negative());
}

TEST(Money, ZeroIsNotNegative) {
    Money m(0.0);
    EXPECT_FALSE(m.is_negative());
}

TEST(Money, Addition) {
    Money a(1.5);
    Money b(2.5);
    Money c = a + b;
    EXPECT_NEAR(c.get_yuan(), 4.0, 1e-9);
}

TEST(Money, AdditionWithNegative) {
    Money a(10.0);
    Money b(-3.0);
    Money c = a + b;
    EXPECT_NEAR(c.get_yuan(), 7.0, 1e-9);
}
