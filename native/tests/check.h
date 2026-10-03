#pragma once

// A deliberately tiny test harness: CHECK records a failure with its location and keeps going, so
// one run reports every broken expectation rather than the first.

#include <cstdio>
#include <functional>
#include <vector>

namespace ssbtest
{
struct Case
{
    const char* name;
    std::function<void()> run;
};

inline std::vector<Case>& cases()
{
    static std::vector<Case> all;
    return all;
}

inline int& failures()
{
    static int count = 0;
    return count;
}

struct Register
{
    Register (const char* name, std::function<void()> run) { cases().push_back ({ name, std::move (run) }); }
};
} // namespace ssbtest

#define SSB_CAT2(a, b) a##b
#define SSB_CAT(a, b) SSB_CAT2 (a, b)
#define TEST(name)                                                                                  \
    static void SSB_CAT (test_, __LINE__)();                                                        \
    static ssbtest::Register SSB_CAT (reg_, __LINE__) (name, SSB_CAT (test_, __LINE__));            \
    static void SSB_CAT (test_, __LINE__)()

#define CHECK(expr)                                                                                 \
    do                                                                                              \
    {                                                                                               \
        if (! (expr))                                                                               \
        {                                                                                           \
            std::fprintf (stderr, "  FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr);                 \
            ++ssbtest::failures();                                                                  \
        }                                                                                           \
    } while (false)

#define CHECK_NEAR(a, b, eps) CHECK (((a) > (b) ? (a) - (b) : (b) - (a)) <= (eps))
