#include "test_support.h"

#include "unity/il2cpp_export_policy.h"

// Binding policy decides whether a mod starts at all, so every combination is
// pinned here: a required export that fails must stop startup, an optional one
// must only disable its own entry.
URK_TEST(required_export_present_and_valid_binds) {
    const auto decision =
        Il2CppExportPolicy_Decide(Il2CppExportRequirement::Required, /*present=*/true, /*targetValid=*/true);
    URK_CHECK(decision.bind);
    URK_CHECK(!decision.failStartup);
}

URK_TEST(required_export_missing_fails_startup) {
    const auto missing =
        Il2CppExportPolicy_Decide(Il2CppExportRequirement::Required, /*present=*/false, /*targetValid=*/true);
    URK_CHECK(!missing.bind);
    URK_CHECK(missing.failStartup);

    const auto invalid =
        Il2CppExportPolicy_Decide(Il2CppExportRequirement::Required, /*present=*/true, /*targetValid=*/false);
    URK_CHECK(!invalid.bind);
    URK_CHECK(invalid.failStartup);
}

URK_TEST(optional_export_missing_only_disables_itself) {
    const auto missing =
        Il2CppExportPolicy_Decide(Il2CppExportRequirement::Optional, /*present=*/false, /*targetValid=*/true);
    URK_CHECK(!missing.bind);
    URK_CHECK(!missing.failStartup);

    const auto invalid =
        Il2CppExportPolicy_Decide(Il2CppExportRequirement::Optional, /*present=*/true, /*targetValid=*/false);
    URK_CHECK(!invalid.bind);
    URK_CHECK(!invalid.failStartup);
}

URK_TEST(optional_export_present_and_valid_binds) {
    const auto decision =
        Il2CppExportPolicy_Decide(Il2CppExportRequirement::Optional, /*present=*/true, /*targetValid=*/true);
    URK_CHECK(decision.bind);
    URK_CHECK(!decision.failStartup);
}

URK_TEST(shared_exact_targets_are_accepted) {
    // Identical bodies fold into one address; two export names may share it.
    URK_CHECK(Il2CppExportPolicy_AcceptSharedExactTarget());
}

