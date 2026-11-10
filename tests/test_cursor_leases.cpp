#include "test_support.h"

#include "cursor_lease_registry.h"

// Cursor leases are per module. One mod must never release another mod's lease,
// and unloading must drop everything that module held.
URK_TEST(cursor_lease_acquire_and_release_are_counted_per_owner) {
    CursorLeaseRegistry registry;
    int moduleA = 0;
    int moduleB = 0;

    URK_CHECK(!registry.AnyOpen());
    URK_CHECK(registry.Acquire(&moduleA));
    URK_CHECK(registry.Acquire(&moduleA));
    URK_CHECK_EQ(registry.LeaseCount(&moduleA), std::size_t{2});
    URK_CHECK(registry.AnyOpen());

    URK_CHECK(registry.Release(&moduleA));
    URK_CHECK_EQ(registry.LeaseCount(&moduleA), std::size_t{1});
    URK_CHECK(registry.AnyOpen());
    URK_CHECK(registry.Release(&moduleA));
    URK_CHECK_EQ(registry.LeaseCount(&moduleA), std::size_t{0});
    URK_CHECK(!registry.AnyOpen());

    // A release from a module that holds nothing is rejected, not a crash.
    URK_CHECK(!registry.Release(&moduleB));
    URK_CHECK(!registry.AnyOpen());
}

URK_TEST(cursor_lease_release_owner_drops_every_lease) {
    CursorLeaseRegistry registry;
    int moduleA = 0;
    int moduleB = 0;
    registry.Acquire(&moduleA);
    registry.Acquire(&moduleA);
    registry.Acquire(&moduleA);
    registry.Acquire(&moduleB);

    URK_CHECK_EQ(registry.ReleaseOwner(&moduleA), std::size_t{3});
    URK_CHECK_EQ(registry.LeaseCount(&moduleA), std::size_t{0});
    URK_CHECK(registry.AnyOpen());
    URK_CHECK_EQ(registry.ReleaseOwner(&moduleA), std::size_t{0});
    URK_CHECK_EQ(registry.OwnerCount(), std::size_t{1});
    URK_CHECK_EQ(registry.ReleaseOwner(&moduleB), std::size_t{1});
    URK_CHECK(!registry.AnyOpen());
}

URK_TEST(cursor_lease_set_replaces_count_instead_of_accumulating) {
    CursorLeaseRegistry registry;
    int module = 0;
    // Set() is the per-frame path: repeating it must not pile up leases.
    for (int frame = 0; frame < 100; ++frame)
        URK_CHECK(registry.Set(&module, true));
    URK_CHECK_EQ(registry.LeaseCount(&module), std::size_t{1});

    URK_CHECK(registry.Set(&module, false));
    URK_CHECK(!registry.AnyOpen());

    // Releasing an owner that used Set() still works.
    URK_CHECK(registry.Set(&module, true));
    URK_CHECK_EQ(registry.ReleaseOwner(&module), std::size_t{1});
    URK_CHECK(!registry.AnyOpen());
}

URK_TEST(cursor_lease_null_owner_is_ignored) {
    CursorLeaseRegistry registry;
    URK_CHECK(!registry.Acquire(nullptr));
    URK_CHECK(!registry.Release(nullptr));
    URK_CHECK(!registry.Set(nullptr, true));
    URK_CHECK_EQ(registry.ReleaseOwner(nullptr), std::size_t{0});
    URK_CHECK(!registry.AnyOpen());
}

URK_TEST(cursor_lease_clear_drops_everything) {
    CursorLeaseRegistry registry;
    int moduleA = 0;
    int moduleB = 0;
    registry.Acquire(&moduleA);
    registry.Acquire(&moduleB);
    URK_CHECK_EQ(registry.OwnerCount(), std::size_t{2});
    registry.Clear();
    URK_CHECK(!registry.AnyOpen());
    URK_CHECK_EQ(registry.OwnerCount(), std::size_t{0});
}
