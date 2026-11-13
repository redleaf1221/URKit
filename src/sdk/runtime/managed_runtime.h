#pragma once

// Backend-neutral names for the managed runtime. URKit ships the IL2CPP backend
// only, so `URK::managed` is an alias rather than a switch: mods written
// against the older generated SDK keep compiling, and new code can still say
// either name.
#include "sdk/il2cpp/il2cpp_runtime.h"

namespace URK {
namespace managed = il2cpp;
inline constexpr RuntimeBackend compiled_runtime_backend = runtime_backend_il2cpp;
inline constexpr const char *compiled_runtime_name = "IL2CPP";
} // namespace URK
