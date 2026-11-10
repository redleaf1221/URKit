// Compile check for the SDK surface. This translation unit exists so the
// public headers are compiled as ordinary sources in every build instead of
// being assembled from strings at generation time.
#include "../sdk/runtime_api.h"

#include "../sdk/coroutines.h"
#include "../sdk/events.h"
#include "../sdk/hook_api.h"
#include "../sdk/il2cpp/il2cpp_helpers.h"
#include "../sdk/il2cpp/il2cpp_runtime.h"
#include "../sdk/mod_async.h"
#include "../sdk/network_api.h"
#include "../sdk/runtime_bootstrap.h"
#include "../sdk/unity/unity.h"

namespace urk::sdk_check {

// Region capabilities are compile-time predicates; instantiate a few so the
// templates are type-checked rather than only parsed.
constexpr bool il2cpp_api_is_known = URK_IL2CPP_API_VERSION > 0;

static_assert(il2cpp_api_is_known);

void ReferenceSymbols() {
    // Address-only: never called. Keeps link-time symbol checks honest.
    (void)&URK::il2cpp::available;
    (void)&URK::il2cpp::find_class;
    (void)&URK::il2cpp::resolve_method;
    (void)&URK::has_il2cpp_api;
}

} // namespace urk::sdk_check
