#include "test_support.h"

#include "sdk/unity/unity_types.h"

// The POD mirrors are marshalled straight into managed structs, so their layout
// and the arithmetic mods use on them are worth pinning.
URK_TEST(vector3_layout_matches_unity) {
    URK_CHECK_EQ(sizeof(URK::Unity::Vector3), std::size_t{12});
    URK_CHECK_EQ(offsetof(URK::Unity::Vector3, x), std::size_t{0});
    URK_CHECK_EQ(offsetof(URK::Unity::Vector3, y), std::size_t{4});
    URK_CHECK_EQ(offsetof(URK::Unity::Vector3, z), std::size_t{8});
    URK_CHECK_EQ(sizeof(URK::Unity::Vector2), std::size_t{8});
    URK_CHECK_EQ(sizeof(URK::Unity::Quaternion), std::size_t{16});
    URK_CHECK_EQ(sizeof(URK::Unity::Color), std::size_t{16});
}

URK_TEST(vector3_arithmetic) {
    using URK::Unity::Vector3;
    const Vector3 a{1.0f, 2.0f, 3.0f};
    const Vector3 b{4.0f, 5.0f, 6.0f};

    const Vector3 sum = a + b;
    URK_CHECK_NEAR(sum.x, 5.0, 1e-6);
    URK_CHECK_NEAR(sum.y, 7.0, 1e-6);
    URK_CHECK_NEAR(sum.z, 9.0, 1e-6);

    const Vector3 scaled = a * 2.0f;
    URK_CHECK_NEAR(scaled.x, 2.0, 1e-6);
    URK_CHECK_NEAR(scaled.y, 4.0, 1e-6);
    URK_CHECK_NEAR(scaled.z, 6.0, 1e-6);

    URK_CHECK_NEAR(Vector3::dot(a, b), 32.0, 1e-6);

    const Vector3 cross = Vector3::cross(Vector3{1.0f, 0.0f, 0.0f}, Vector3{0.0f, 1.0f, 0.0f});
    URK_CHECK_NEAR(cross.x, 0.0, 1e-6);
    URK_CHECK_NEAR(cross.y, 0.0, 1e-6);
    URK_CHECK_NEAR(cross.z, 1.0, 1e-6);

    const Vector3 origin{0.0f, 0.0f, 0.0f};
    const Vector3 offset{3.0f, 4.0f, 0.0f};
    const float distance = Vector3::distance(origin, offset);
    URK_CHECK_NEAR(distance, 5.0, 1e-6);
}

URK_TEST(vector3_division_by_zero_and_normalize_degenerate) {
    using URK::Unity::Vector3;
    const Vector3 zero = Vector3{1.0f, 1.0f, 1.0f} / 0.0f;
    URK_CHECK_NEAR(zero.x, 0.0, 1e-6);
    URK_CHECK_NEAR(zero.y, 0.0, 1e-6);
    URK_CHECK_NEAR(zero.z, 0.0, 1e-6);

    // A zero-length vector normalizes to zero rather than producing NaNs that
    // would poison a draw call.
    const Vector3 normalized = Vector3{}.normalized();
    URK_CHECK_NEAR(normalized.x, 0.0, 1e-6);
    URK_CHECK_NEAR(normalized.y, 0.0, 1e-6);
    URK_CHECK_NEAR(normalized.z, 0.0, 1e-6);

    const Vector3 forward{0.0f, 0.0f, 4.0f};
    const Vector3 unit = forward.normalized();
    URK_CHECK_NEAR(unit.x, 0.0, 1e-6);
    URK_CHECK_NEAR(unit.y, 0.0, 1e-6);
    URK_CHECK_NEAR(unit.z, 1.0, 1e-6);
}

URK_TEST(vector3_nearly_zero_uses_epsilon) {
    using URK::Unity::Vector3;
    const Vector3 zero{};
    const Vector3 tiny{1e-8f, 0.0f, 0.0f};
    const Vector3 visible{0.01f, 0.0f, 0.0f};
    URK_CHECK(zero.nearly_zero());
    URK_CHECK(tiny.nearly_zero());
    URK_CHECK(!visible.nearly_zero());
}
