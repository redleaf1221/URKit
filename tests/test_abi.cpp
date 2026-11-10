#include "test_support.h"

#include "sdk/mod_sdk.h"

#include <cstddef>

// The ABI is the one contract every loader/mod pair depends on. These checks
// are the runtime half of the static_asserts in mod_sdk.h: they pin the values
// that a silent reordering or a renumbered flag would break.
URK_TEST(abi_versions_are_positive) {
    URK_CHECK(URK_SDK_VERSION > 0);
    URK_CHECK(URK_IL2CPP_API_VERSION > 0);
    URK_CHECK(URK_RUNTIME_API_VERSION > 0);
    URK_CHECK(URK_NETWORK_API_VERSION > 0);
}

URK_TEST(abi_backend_ids_are_stable) {
    URK_CHECK_EQ(static_cast<int>(URK_RUNTIME_BACKEND_UNKNOWN), 0);
    URK_CHECK_EQ(static_cast<int>(URK_RUNTIME_BACKEND_IL2CPP), 2);
}

URK_TEST(abi_capability_bits_are_distinct) {
    const uint64_t flags[] = {
        URK_RUNTIME_CAP_IL2CPP_API,    URK_RUNTIME_CAP_HOOKS,       URK_RUNTIME_CAP_MAIN_THREAD,
        URK_RUNTIME_CAP_SCENE_EVENTS,  URK_RUNTIME_CAP_CURSOR_CONTROL, URK_RUNTIME_CAP_NETWORK,
        URK_RUNTIME_CAP_INPUT,         URK_RUNTIME_CAP_GRAPHICS_DEVICE_TYPE, URK_RUNTIME_CAP_OBJECT_DESTROY_REQUEST_EVENTS,
        URK_RUNTIME_CAP_STEAM_IDENTITY, URK_RUNTIME_CAP_MID_HOOKS,
    };
    for (std::size_t i = 0; i < sizeof(flags) / sizeof(flags[0]); ++i) {
        URK_CHECK(flags[i] != 0);
        for (std::size_t j = i + 1; j < sizeof(flags) / sizeof(flags[0]); ++j)
            URK_CHECK(flags[i] != flags[j]);
    }
}

URK_TEST(abi_mod_context_keeps_version_and_size_first) {
    // Loader and mod both size-check the context before touching any entry.
    URK_CHECK_EQ(offsetof(URK_ModContext, version), std::size_t{0});
    URK_CHECK(offsetof(URK_ModContext, size) > offsetof(URK_ModContext, version));
    URK_CHECK(offsetof(URK_ModContext, size) < sizeof(URK_ModContext));
}

URK_TEST(abi_mod_context_appends_new_tables) {
    // The order below is the compatibility promise: new pointers go at the end.
    URK_CHECK(offsetof(URK_ModContext, runtime) > offsetof(URK_ModContext, runtimeCapabilities));
    URK_CHECK(offsetof(URK_ModContext, il2cpp) > offsetof(URK_ModContext, runtime));
    URK_CHECK(offsetof(URK_ModContext, network) > offsetof(URK_ModContext, gameAssemblyModuleBase));
    URK_CHECK(offsetof(URK_ModContext, hooks) > offsetof(URK_ModContext, network));
}

URK_TEST(abi_api_tables_keep_version_and_size_first) {
    URK_CHECK_EQ(offsetof(URK_Il2CppApi, version), std::size_t{0});
    URK_CHECK(offsetof(URK_Il2CppApi, size) > offsetof(URK_Il2CppApi, version));
    URK_CHECK(offsetof(URK_Il2CppApi, is_available) > offsetof(URK_Il2CppApi, size));
    URK_CHECK(offsetof(URK_Il2CppApi, last_error) > offsetof(URK_Il2CppApi, field_offset));

    URK_CHECK(offsetof(URK_RuntimeApi, size) > offsetof(URK_RuntimeApi, version));
    URK_CHECK(offsetof(URK_RuntimeApi, is_main_thread) > offsetof(URK_RuntimeApi, menu_mouse_capture_set_owned));

    URK_CHECK(offsetof(URK_HookApi, mid_attach) > offsetof(URK_HookApi, size));
    URK_CHECK(offsetof(URK_NetworkApi, json_request) > offsetof(URK_NetworkApi, size));
}

URK_TEST(abi_scene_info_is_plain_and_terminated) {
    URK_CHECK_EQ(URK_SCENE_NAME_MAX, 128);
    URK_CHECK_EQ(URK_OBJECT_NAME_MAX, 128);
    URK_CHECK(offsetof(URK_SceneInfo, size) == 0);
    URK_CHECK(offsetof(URK_SceneInfo, name) > offsetof(URK_SceneInfo, handle));
}

URK_TEST(abi_hook_backends_are_stable) {
    URK_CHECK_EQ(static_cast<int>(URK_HOOK_BACKEND_AUTO), 0);
    URK_CHECK_EQ(static_cast<int>(URK_HOOK_BACKEND_DETOURS), 1);
    URK_CHECK_EQ(static_cast<int>(URK_HOOK_BACKEND_SAFETYHOOK), 2);
}

