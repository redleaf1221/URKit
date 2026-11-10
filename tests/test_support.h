#pragma once

#include <string>
#include <vector>

// A deliberately small harness: registration, assertion and a non-zero exit
// code. No third-party dependency and no extra build step.
namespace urk_test {

struct Case {
    const char *name;
    void (*run)();
};

std::vector<Case> &Registry();

struct Registrar {
    Registrar(const char *name, void (*run)());
};

void Fail(const char *file, int line, const std::string &message);
int RunAll(const char *suite_name);

inline std::string Detail(const std::string &value) {
    return value;
}
inline std::string Detail(const char *value) {
    return value ? value : "<null>";
}
inline std::string Detail(bool value) {
    return value ? "true" : "false";
}
template <typename T> std::string Detail(const T &value) {
    return std::to_string(value);
}

} // namespace urk_test

#define URK_TEST(name)                                                                                                 \
    static void name();                                                                                                \
    static ::urk_test::Registrar name##_registrar(#name, &name);                                                       \
    static void name()

#define URK_CHECK(condition)                                                                                           \
    do {                                                                                                               \
        if (!(condition))                                                                                              \
            ::urk_test::Fail(__FILE__, __LINE__, "expected " #condition);                                              \
    } while (false)

#define URK_CHECK_EQ(actual, expected)                                                                                 \
    do {                                                                                                               \
        const auto urk_actual = (actual);                                                                              \
        const auto urk_expected = (expected);                                                                          \
        if (!(urk_actual == urk_expected))                                                                             \
            ::urk_test::Fail(__FILE__, __LINE__,                                                                       \
                             std::string(#actual " == " #expected ", got <") + ::urk_test::Detail(urk_actual) + ">");  \
    } while (false)

#define URK_CHECK_NEAR(actual, expected, tolerance)                                                                    \
    do {                                                                                                               \
        const double urk_actual = (actual);                                                                            \
        const double urk_expected = (expected);                                                                        \
        const double urk_delta = urk_actual - urk_expected;                                                            \
        if (!(urk_delta < (tolerance) && -urk_delta < (tolerance)))                                                    \
            ::urk_test::Fail(__FILE__, __LINE__,                                                                       \
                             std::string(#actual " ~= " #expected ", got ") + std::to_string(urk_actual));            \
    } while (false)
