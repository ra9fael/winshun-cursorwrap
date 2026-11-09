// test_harness.h — a minimal Catch2-style test harness with no external
// dependencies. One process runs all tests; failures print and count.
#pragma once

#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace cwtest {

struct TestCase {
    const char* name;
    std::function<void()> fn;
};

inline std::vector<TestCase>& registry()
{
    static std::vector<TestCase> t;
    return t;
}

inline int& failures()
{
    static int f = 0;
    return f;
}

template <typename Fn>
inline bool registerTest(const char* name, Fn fn)
{
    registry().push_back({name, std::function<void()>(fn)});
    return true;
}

inline int runAll()
{
    for (const TestCase& t : registry()) {
        std::printf("[ RUN  ] %s\n", t.name);
        std::fflush(stdout);
        const int before = failures();
        t.fn();
        if (failures() == before)
            std::printf("[  OK  ] %s\n", t.name);
        else
            std::printf("[ FAIL ] %s\n", t.name);
        std::fflush(stdout);
    }
    std::printf("\n%s (%d failure%s)\n", failures() == 0 ? "all tests passed" : "TESTS FAILED",
                failures(), failures() == 1 ? "" : "s");
    return failures() == 0 ? 0 : 1;
}

} // namespace cwtest

#define CW_TEST(name)                                                              \
    static void cw_test_##name();                                                  \
    static const bool cw_reg_##name = cwtest::registerTest(#name, cw_test_##name); \
    static void cw_test_##name()

#define CW_CHECK(cond)                                                             \
    do {                                                                           \
        if (!(cond)) {                                                             \
            ++cwtest::failures();                                                  \
            std::printf("  check failed: %s (%s:%d)\n", #cond, __FILE__, __LINE__); \
        }                                                                          \
    } while (0)

#define CW_CHECK_EQ(a, b)                                                          \
    do {                                                                           \
        const auto va_ = (a);                                                      \
        const auto vb_ = (b);                                                      \
        if (!(va_ == vb_)) {                                                       \
            ++cwtest::failures();                                                  \
            std::printf("  check failed: %s == %s (%s:%d)\n", #a, #b, __FILE__,    \
                        __LINE__);                                                 \
        }                                                                          \
    } while (0)
