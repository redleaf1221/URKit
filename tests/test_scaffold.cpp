#include "test_support.h"

#include "scaffold.h"

// `urk new` turns a human name into a directory, a CMake target and a mod id.
// A bad name produces a project that will not configure, so the rules are
// pinned here.
URK_TEST(mod_name_keeps_valid_identifiers) {
    URK_CHECK_EQ(urk::SanitizeModName("hello"), std::string("hello"));
    URK_CHECK_EQ(urk::SanitizeModName("HelloMod"), std::string("HelloMod"));
    URK_CHECK_EQ(urk::SanitizeModName("mod_2"), std::string("mod_2"));
}

URK_TEST(mod_name_replaces_separators) {
    URK_CHECK_EQ(urk::SanitizeModName("my mod"), std::string("my_mod"));
    URK_CHECK_EQ(urk::SanitizeModName("my-mod"), std::string("my_mod"));
    URK_CHECK_EQ(urk::SanitizeModName("my.mod"), std::string("mymod"));
    URK_CHECK_EQ(urk::SanitizeModName("a/b"), std::string("ab"));
}

URK_TEST(mod_name_never_starts_with_a_digit) {
    URK_CHECK_EQ(urk::SanitizeModName("123abc"), std::string("_123abc"));
    URK_CHECK_EQ(urk::SanitizeModName("7"), std::string("_7"));
}

URK_TEST(mod_name_falls_back_when_empty) {
    URK_CHECK_EQ(urk::SanitizeModName(""), std::string("mod"));
    URK_CHECK_EQ(urk::SanitizeModName("!!!"), std::string("mod"));
    URK_CHECK_EQ(urk::SanitizeModName("---"), std::string("___"));
}

URK_TEST(mod_id_is_name_plus_suffix) {
    URK_CHECK_EQ(urk::MakeModId("hello", "0123abcd"), std::string("hello_0123abcd"));
    URK_CHECK_EQ(urk::MakeModId("Alias", "ffffffff"), std::string("Alias_ffffffff"));
}

