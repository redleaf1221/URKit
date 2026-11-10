#include "test_support.h"

#include "unity/unity_name_matching.h"

// The loader matches IL2CPP metadata names against user-supplied strings. Every
// rule below has bitten a real lookup: aliases, paths, missing .dll, by-ref.
URK_TEST(type_names_fold_csharp_aliases) {
    using URK::UnityNames::NormalizeTypeName;
    URK_CHECK_EQ(NormalizeTypeName("int"), std::string("system.int32"));
    URK_CHECK_EQ(NormalizeTypeName("Int32"), std::string("system.int32"));
    URK_CHECK_EQ(NormalizeTypeName("System.Int32"), std::string("system.int32"));
    URK_CHECK_EQ(NormalizeTypeName("uint"), std::string("system.uint32"));
    URK_CHECK_EQ(NormalizeTypeName("long"), std::string("system.int64"));
    URK_CHECK_EQ(NormalizeTypeName("float"), std::string("system.single"));
    URK_CHECK_EQ(NormalizeTypeName("string"), std::string("system.string"));
    URK_CHECK_EQ(NormalizeTypeName("bool"), std::string("system.boolean"));
    URK_CHECK_EQ(NormalizeTypeName("void"), std::string("system.void"));
}

URK_TEST(type_names_keep_byref_and_pointer_suffixes) {
    using URK::UnityNames::NormalizeTypeName;
    URK_CHECK_EQ(NormalizeTypeName("System.Int32&"), std::string("system.int32&"));
    URK_CHECK_EQ(NormalizeTypeName("Int32&"), std::string("system.int32&"));
    URK_CHECK_EQ(NormalizeTypeName("System.Int32*"), std::string("system.int32*"));
    URK_CHECK_EQ(NormalizeTypeName("int&&"), std::string("system.int32&&"));
}

URK_TEST(type_names_strip_class_and_struct_prefixes) {
    using URK::UnityNames::NormalizeTypeName;
    URK_CHECK_EQ(NormalizeTypeName("class System.String"), std::string("system.string"));
    URK_CHECK_EQ(NormalizeTypeName("struct System.Int32"), std::string("system.int32"));
    // Only the exact CLR prefixes are stripped, not arbitrary words.
    URK_CHECK_EQ(NormalizeTypeName("classes.MyType"), std::string("classes.mytype"));
}

URK_TEST(type_name_matches_is_case_insensitive_and_null_safe) {
    using URK::UnityNames::TypeNameMatches;
    URK_CHECK(TypeNameMatches("System.Int32", "int"));
    URK_CHECK(TypeNameMatches("system.int32", "System.Int32"));
    URK_CHECK(TypeNameMatches("UnityEngine.Vector3", "UnityEngine.Vector3"));
    URK_CHECK(!TypeNameMatches("System.Int32", "System.Int64"));
    URK_CHECK(!TypeNameMatches(nullptr, "int"));
    URK_CHECK(!TypeNameMatches("int", nullptr));
    // An array type is a different type, not a spelling of the element type.
    URK_CHECK(!TypeNameMatches("System.Int32[]", "int"));
}

URK_TEST(image_variants_cover_path_and_extension) {
    using URK::UnityNames::ImageNameVariants;
    const auto variants = ImageNameVariants("C:\\Games\\Example\\Assembly-CSharp.dll");
    URK_CHECK_EQ(variants.size(), std::size_t{4});
    URK_CHECK_EQ(variants[0], std::string("c:\\games\\example\\assembly-csharp.dll"));
    URK_CHECK_EQ(variants[1], std::string("c:\\games\\example\\assembly-csharp"));
    URK_CHECK_EQ(variants[2], std::string("assembly-csharp.dll"));
    URK_CHECK_EQ(variants[3], std::string("assembly-csharp"));
}

URK_TEST(image_name_matches_accepts_any_legitimate_spelling) {
    using URK::UnityNames::ImageNameMatches;
    URK_CHECK(ImageNameMatches("Assembly-CSharp", "Assembly-CSharp.dll"));
    URK_CHECK(ImageNameMatches("assembly-csharp.dll", "Assembly-CSharp"));
    URK_CHECK(ImageNameMatches("C:/x/y/Assembly-CSharp.dll", "Assembly-CSharp"));
    URK_CHECK(!ImageNameMatches("Assembly-CSharp", "UnityEngine.CoreModule.dll"));
    URK_CHECK(!ImageNameMatches("", "Assembly-CSharp.dll"));
}

URK_TEST(image_variants_ignore_empty_input) {
    using URK::UnityNames::ImageNameVariants;
    URK_CHECK(ImageNameVariants(nullptr).empty());
    URK_CHECK(ImageNameVariants("").empty());
}
