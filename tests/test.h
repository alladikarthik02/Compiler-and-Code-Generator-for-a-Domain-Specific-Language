// Minimal, dependency-free test harness. Each test file defines suites and
// runs them from main(); a non-zero exit code signals failure to ctest.
#pragma once
#include <cstdio>
#include <string>
#include <sstream>

namespace dsltest {

inline int& failures() { static int f = 0; return f; }
inline int& checks()   { static int c = 0; return c; }

inline void report_fail(const char* file, int line, const std::string& msg) {
    ++failures();
    std::fprintf(stderr, "  FAIL %s:%d: %s\n", file, line, msg.c_str());
}

template <typename A, typename B>
void check_eq(const A& a, const B& b, const char* ea, const char* eb,
              const char* file, int line) {
    ++checks();
    if (!(a == b)) {
        std::ostringstream os;
        os << "CHECK_EQ(" << ea << ", " << eb << ")  [" << a << " != " << b << "]";
        report_fail(file, line, os.str());
    }
}

inline void check(bool cond, const char* expr, const char* file, int line) {
    ++checks();
    if (!cond) report_fail(file, line, std::string("CHECK(") + expr + ")");
}

inline int summary(const char* name) {
    std::fprintf(stderr, "[%s] %d checks, %d failures\n", name, checks(), failures());
    return failures() == 0 ? 0 : 1;
}

} // namespace dsltest

#define CHECK(cond)       ::dsltest::check((cond), #cond, __FILE__, __LINE__)
#define CHECK_EQ(a, b)    ::dsltest::check_eq((a), (b), #a, #b, __FILE__, __LINE__)
#define TEST_SUMMARY(nm)  return ::dsltest::summary(nm)
