#pragma once

// A check macro and nothing else: the suites are plain programs that print what
// failed and exit non-zero, so they run under g++ with no framework.

#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>

namespace Check {
inline int &failures()
{
    static int n = 0;
    return n;
}
inline int &passes()
{
    static int n = 0;
    return n;
}
} // namespace Check

#define CHECK(cond)                                                                                  \
    do {                                                                                             \
        if (cond) {                                                                                  \
            ++Check::passes();                                                                       \
        } else {                                                                                     \
            ++Check::failures();                                                                     \
            std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);                     \
        }                                                                                            \
    } while (0)

#define CHECK_EQ(a, b)                                                                               \
    do {                                                                                             \
        const auto va__ = (a);                                                                       \
        const auto vb__ = (b);                                                                       \
        if (va__ == vb__) {                                                                          \
            ++Check::passes();                                                                       \
        } else {                                                                                     \
            ++Check::failures();                                                                     \
            std::ostringstream o__;                                                                  \
            o__ << va__ << " != " << vb__;                                                           \
            std::fprintf(stderr, "FAIL %s:%d  %s == %s  (%s)\n", __FILE__, __LINE__, #a, #b,         \
                         o__.str().c_str());                                                         \
        }                                                                                            \
    } while (0)

#define CHECK_NEAR(a, b, tol)                                                                        \
    do {                                                                                             \
        const double va__ = (a);                                                                     \
        const double vb__ = (b);                                                                     \
        if (va__ - vb__ <= (tol) && vb__ - va__ <= (tol)) {                                          \
            ++Check::passes();                                                                       \
        } else {                                                                                     \
            ++Check::failures();                                                                     \
            std::fprintf(stderr, "FAIL %s:%d  %s ~ %s  (%g vs %g)\n", __FILE__, __LINE__, #a, #b,    \
                         va__, vb__);                                                                \
        }                                                                                            \
    } while (0)

inline int checkSummary(const char *suite)
{
    std::printf("%s: %d checks, %d failed\n", suite, Check::passes() + Check::failures(), Check::failures());
    return Check::failures() == 0 ? 0 : 1;
}
