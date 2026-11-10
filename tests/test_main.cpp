#include "test_support.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace urk_test {
namespace {
int &Failures() {
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
    ++Failures();
    std::fprintf(stderr, "FAIL %s:%d %s\n", file, line, message.c_str());
}

int FailureCount() {
    return Failures();
}

} // namespace urk_test

namespace {

// CTest runs one process per case (see cmake/DiscoverUrkTests.cmake), so this
// file only needs to list the registered names, run the requested subset, and
// return a status.
bool Matches(const urk_test::Case &test, const std::string &exact, const std::string &substring) {
    if (!exact.empty())
        return exact == test.name;
    if (!substring.empty())
        return std::strstr(test.name, substring.c_str()) != nullptr;
    return true;
}

void PrintUsage() {
    std::printf("usage: urk_tests [--list | <case-name> | --filter <substring> | --help]\n"
                "  (no argument)          run every registered case\n"
                "  --list                 print one case name per line; used by CTest discovery\n"
                "  <case-name>            run exactly that case, as CTest passes it\n"
                "  --filter <substring>   run every case whose name contains the substring\n");
}

} // namespace

int main(int argc, char **argv) {
    std::string exact;
    std::string substring;
    bool list_only = false;

    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--list") {
            list_only = true;
        } else if (argument == "--filter" && i + 1 < argc) {
            substring = argv[++i];
        } else if (argument == "--help" || argument == "-h") {
            PrintUsage();
            return EXIT_SUCCESS;
        } else {
            exact = argument;
        }
    }

    if (list_only) {
        for (const urk_test::Case &test : urk_test::Registry())
            std::printf("%s\n", test.name);
        return EXIT_SUCCESS;
    }

    int selected = 0;
    int failed = 0;
    for (const urk_test::Case &test : urk_test::Registry()) {
        if (!Matches(test, exact, substring))
            continue;
        ++selected;
        const int before = urk_test::FailureCount();
        test.run();
        const bool ok = urk_test::FailureCount() == before;
        std::printf("%s %s\n", ok ? "PASS" : "FAIL", test.name);
        if (!ok)
            ++failed;
    }

    if (selected == 0) {
        std::fprintf(stderr, "no case matches '%s'; run urk_tests --list\n",
                     exact.empty() ? substring.c_str() : exact.c_str());
        return EXIT_FAILURE;
    }
    if (failed != 0) {
        std::printf("%d of %d case(s) failed\n", failed, selected);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
