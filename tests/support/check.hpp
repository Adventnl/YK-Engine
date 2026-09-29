#pragma once
// Minimal assertion helpers shared by the test executables. A failed CHECK reports the expression
// and location, continues, and makes the process exit nonzero via finish().
#include <cmath>
#include <cstdio>
#include <string>

namespace yk::test {
inline int failures = 0;
inline int checks = 0;
inline void record(bool passed, const char *expression, const char *file, int line) {
    ++checks;
    if (!passed) {
        ++failures;
        std::fprintf(stderr, "FAIL %s:%d: %s\n", file, line, expression);
    }
}
inline bool near(double a, double b, double tolerance = 1e-4) {
    return std::abs(a - b) <= tolerance;
}
inline int finish(const char *suite) {
    std::printf("%s: %d checks, %d failures\n", suite, checks, failures);
    return failures == 0 ? 0 : 1;
}
} // namespace yk::test

#define CHECK(expression)                                                                          \
    ::yk::test::record(static_cast<bool>(expression), #expression, __FILE__, __LINE__)
#define CHECK_NEAR(a, b, ...)                                                                      \
    ::yk::test::record(::yk::test::near((a), (b), ##__VA_ARGS__), "near(" #a ", " #b ")",          \
                       __FILE__, __LINE__)
