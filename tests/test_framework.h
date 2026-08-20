#pragma once

// ============================================================
// test_framework.h
// 轻量级、零依赖的 C++ 单元测试框架（GoogleTest 风格 API）
//
// 用法：
//   TEST(SuiteName, TestName) {
//       EXPECT_EQ(1 + 1, 2);
//       EXPECT_TRUE(some_condition);
//   }
// 在 test_main.cpp 中定义 main() 并调用 testfw::run_all_tests()。
//
// 与 GoogleTest 的差异：
//   - 无 TEST_F / ASSERT_*（仅提供 EXPECT_* 系列与 EXPECT_THROW）
//   - 依赖类型重载 operator<< 以输出诊断信息；枚举请先 static_cast<int>
// ============================================================

#include <cmath>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace testfw {

// ---- 测试注册表 ----
struct TestCase {
    std::string suite;
    std::string name;
    std::function<void()> fn;
};

inline std::vector<TestCase>& tests() {
    static std::vector<TestCase> instance;
    return instance;
}

inline void register_test(const std::string& suite,
                          const std::string& name,
                          std::function<void()> fn) {
    tests().push_back({suite, name, std::move(fn)});
}

// 当前测试的失败信息（每个用例运行前清空）
inline std::vector<std::string>& failures() {
    static std::vector<std::string> instance;
    return instance;
}

// 断言失败时抛出，用于中止当前测试用例
struct assertion_failure {};

[[noreturn]] inline void report_failure(const char* file, int line, const std::string& message) {
    std::ostringstream oss;
    oss << file << ":" << line << ": " << message;
    failures().push_back(oss.str());
    throw assertion_failure{};
}

// ---- 断言实现（需要类型支持 operator<< 输出诊断信息）----
template <typename A, typename B>
void expect_eq(const A& a, const B& b, const char* file, int line,
               const char* a_text, const char* b_text) {
    if (!(a == b)) {
        std::ostringstream oss;
        oss << "EXPECT_EQ(" << a_text << ", " << b_text << ") failed: "
            << a << " != " << b;
        report_failure(file, line, oss.str());
    }
}

template <typename A, typename B>
void expect_ne(const A& a, const B& b, const char* file, int line,
               const char* a_text, const char* b_text) {
    if (a == b) {
        std::ostringstream oss;
        oss << "EXPECT_NE(" << a_text << ", " << b_text << ") failed: "
            << a << " == " << b;
        report_failure(file, line, oss.str());
    }
}

template <typename A, typename B>
void expect_gt(const A& a, const B& b, const char* file, int line,
               const char* a_text, const char* b_text) {
    if (!(a > b)) {
        std::ostringstream oss;
        oss << "EXPECT_GT(" << a_text << ", " << b_text << ") failed: "
            << a << " <= " << b;
        report_failure(file, line, oss.str());
    }
}

template <typename A, typename B>
void expect_lt(const A& a, const B& b, const char* file, int line,
               const char* a_text, const char* b_text) {
    if (!(a < b)) {
        std::ostringstream oss;
        oss << "EXPECT_LT(" << a_text << ", " << b_text << ") failed: "
            << a << " >= " << b;
        report_failure(file, line, oss.str());
    }
}

template <typename A, typename B>
void expect_ge(const A& a, const B& b, const char* file, int line,
               const char* a_text, const char* b_text) {
    if (!(a >= b)) {
        std::ostringstream oss;
        oss << "EXPECT_GE(" << a_text << ", " << b_text << ") failed: "
            << a << " < " << b;
        report_failure(file, line, oss.str());
    }
}

template <typename A, typename B>
void expect_le(const A& a, const B& b, const char* file, int line,
               const char* a_text, const char* b_text) {
    if (!(a <= b)) {
        std::ostringstream oss;
        oss << "EXPECT_LE(" << a_text << ", " << b_text << ") failed: "
            << a << " > " << b;
        report_failure(file, line, oss.str());
    }
}

template <typename A, typename B, typename T>
void expect_near(const A& a, const B& b, const T& tol, const char* file, int line,
                 const char* a_text, const char* b_text) {
    const double diff = std::fabs(static_cast<double>(a) - static_cast<double>(b));
    if (!(diff <= static_cast<double>(tol))) {
        std::ostringstream oss;
        oss << "EXPECT_NEAR(" << a_text << ", " << b_text << ", " << tol
            << ") failed: |" << a << " - " << b << "| = " << diff << " > " << tol;
        report_failure(file, line, oss.str());
    }
}

// 按字符串内容比较（避免 const char* 比较指针）
template <typename A, typename B>
void expect_str_eq(const A& a, const B& b, const char* file, int line,
                   const char* a_text, const char* b_text) {
    if (std::string(a) != std::string(b)) {
        std::ostringstream oss;
        oss << "EXPECT_STREQ(" << a_text << ", " << b_text << ") failed: "
            << a << " != " << b;
        report_failure(file, line, oss.str());
    }
}

// ---- 测试运行器 ----
inline int run_all_tests() {
    int passed = 0;
    int failed = 0;

    std::cout << "[==========] Running " << tests().size() << " tests." << std::endl;

    std::string current_suite;
    for (const auto& t : tests()) {
        if (t.suite != current_suite) {
            current_suite = t.suite;
            std::cout << "[----------] " << t.suite << std::endl;
        }

        std::cout << "[ RUN      ] " << t.suite << "." << t.name << std::endl;
        failures().clear();
        try {
            t.fn();
            std::cout << "[       OK ] " << t.suite << "." << t.name << std::endl;
            ++passed;
        } catch (const assertion_failure&) {
            std::cout << "[  FAILED  ] " << t.suite << "." << t.name << std::endl;
            for (const auto& f : failures()) {
                std::cout << "      " << f << std::endl;
            }
            ++failed;
        } catch (const std::exception& e) {
            std::cout << "[  FAILED  ] " << t.suite << "." << t.name
                      << " (unexpected exception: " << e.what() << ")" << std::endl;
            ++failed;
        } catch (...) {
            std::cout << "[  FAILED  ] " << t.suite << "." << t.name
                      << " (unknown exception)" << std::endl;
            ++failed;
        }
    }

    std::cout << "[==========] " << (passed + failed) << " tests run, "
              << passed << " passed, " << failed << " failed." << std::endl;
    return failed == 0 ? 0 : 1;
}

}  // namespace testfw

// ---- 测试用例宏 ----
#define TEST(suite, name)                                                       \
    static void suite##_##name();                                               \
    namespace {                                                                 \
    struct suite##_##name##_registrar {                                         \
        suite##_##name##_registrar() {                                          \
            testfw::register_test(#suite, #name, suite##_##name);               \
        }                                                                       \
    } suite##_##name##_registrar_instance;                                      \
    }                                                                           \
    static void suite##_##name()

// ---- 断言宏 ----
#define EXPECT_TRUE(expr)                                                       \
    do {                                                                        \
        if (!(expr)) {                                                          \
            testfw::report_failure(__FILE__, __LINE__,                          \
                                   "EXPECT_TRUE(" #expr ") failed");            \
        }                                                                       \
    } while (0)

#define EXPECT_FALSE(expr)                                                      \
    do {                                                                        \
        if (expr) {                                                             \
            testfw::report_failure(__FILE__, __LINE__,                          \
                                   "EXPECT_FALSE(" #expr ") failed");           \
        }                                                                       \
    } while (0)

#define EXPECT_EQ(a, b)                                                         \
    do { testfw::expect_eq((a), (b), __FILE__, __LINE__, #a, #b); } while (0)

#define EXPECT_NE(a, b)                                                         \
    do { testfw::expect_ne((a), (b), __FILE__, __LINE__, #a, #b); } while (0)

#define EXPECT_GT(a, b)                                                         \
    do { testfw::expect_gt((a), (b), __FILE__, __LINE__, #a, #b); } while (0)

#define EXPECT_LT(a, b)                                                         \
    do { testfw::expect_lt((a), (b), __FILE__, __LINE__, #a, #b); } while (0)

#define EXPECT_GE(a, b)                                                         \
    do { testfw::expect_ge((a), (b), __FILE__, __LINE__, #a, #b); } while (0)

#define EXPECT_LE(a, b)                                                         \
    do { testfw::expect_le((a), (b), __FILE__, __LINE__, #a, #b); } while (0)

#define EXPECT_NEAR(a, b, tol)                                                  \
    do { testfw::expect_near((a), (b), (tol), __FILE__, __LINE__, #a, #b); }    \
    while (0)

#define EXPECT_STREQ(a, b)                                                      \
    do { testfw::expect_str_eq((a), (b), __FILE__, __LINE__, #a, #b); } while (0)

#define EXPECT_THROW(expr, extype)                                              \
    do {                                                                        \
        bool threw_expected_ = false;                                           \
        try {                                                                   \
            expr;                                                               \
        } catch (const extype&) {                                               \
            threw_expected_ = true;                                             \
        } catch (...) {                                                         \
        }                                                                       \
        if (!threw_expected_) {                                                 \
            testfw::report_failure(__FILE__, __LINE__,                          \
                                   "EXPECT_THROW(" #expr ", " #extype            \
                                   ") failed: no matching exception");          \
        }                                                                       \
    } while (0)

#define EXPECT_ANY_THROW(expr)                                                  \
    do {                                                                        \
        bool threw_any_ = false;                                                \
        try {                                                                   \
            expr;                                                               \
        } catch (...) {                                                         \
            threw_any_ = true;                                                  \
        }                                                                       \
        if (!threw_any_) {                                                      \
            testfw::report_failure(__FILE__, __LINE__,                          \
                                   "EXPECT_ANY_THROW(" #expr                    \
                                   ") failed: no exception thrown");            \
        }                                                                       \
    } while (0)

#define EXPECT_NO_THROW(expr)                                                   \
    do {                                                                        \
        try {                                                                   \
            expr;                                                               \
        } catch (...) {                                                         \
            testfw::report_failure(__FILE__, __LINE__,                          \
                                   "EXPECT_NO_THROW(" #expr                     \
                                   ") failed: exception thrown");               \
        }                                                                       \
    } while (0)

#define FAIL()                                                                  \
    do { testfw::report_failure(__FILE__, __LINE__, "FAIL() called"); } while (0)

#define SUCCEED() ((void)0)
