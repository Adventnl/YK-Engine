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
inline void recordNear(const char *arguments, const char *file, int line, double a, double b,
                       double tolerance = 1e-4) {
    ++checks;
    if (!near(a, b, tolerance)) {
        ++failures;
        std::fprintf(stderr, "FAIL %s:%d: near(%s): %g vs %g\n", file, line, arguments, a, b);
    }
}
inline int finish(const char *suite) {
    std::printf("%s: %d checks, %d failures\n", suite, checks, failures);
    return failures == 0 ? 0 : 1;
}
} // namespace yk::test

// Variadic so that an expression containing braces and commas (Vec2{1, 2}) is one argument; the
// parentheses keep it one expression. Standard __VA_ARGS__ use, not the GNU extension.
#define CHECK(...)                                                                                 \
    ::yk::test::record(static_cast<bool>((__VA_ARGS__)), #__VA_ARGS__, __FILE__, __LINE__)
// CHECK_NEAR(a, b) or CHECK_NEAR(a, b, tolerance); no GNU variadic-macro extension needed.
#define CHECK_NEAR(...) ::yk::test::recordNear(#__VA_ARGS__, __FILE__, __LINE__, __VA_ARGS__)
