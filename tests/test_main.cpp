#include "test_support.h"

#include <cstdio>
#include <cstdlib>

namespace urk_test {
namespace {
int &FailureCount() {
    static int count = 0;
    return count;
}
} // namespace

std::vector<Case> &Registry() {
    static std::vector<Case> cases;
    return cases;
}

Registrar::Registrar(const char *name, void (*run)()) {
    Registry().push_back({name, run});
}

void Fail(const char *file, int line, const std::string &message) {
    ++FailureCount();
    std::fprintf(stderr, "FAIL %s:%d %s\n", file, line, message.c_str());
}

int RunAll(const char *suite_name) {
    std::printf("%s: %zu cases\n", suite_name, Registry().size());
    for (const Case &test : Registry()) {
        const int before = FailureCount();
        test.run();
        std::printf("  %-46s %s\n", test.name, FailureCount() == before ? "ok" : "FAILED");
    }
    if (FailureCount() != 0) {
        std::printf("%s: %d failure(s)\n", suite_name, FailureCount());
        return EXIT_FAILURE;
    }
    std::printf("%s: all cases passed\n", suite_name);
    return EXIT_SUCCESS;
}

} // namespace urk_test

int main() {
    return urk_test::RunAll("urk_tests");
}
