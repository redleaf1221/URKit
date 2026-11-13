#pragma once

// Hook helpers for managed methods, named for the backend-neutral SDK. The
// bodies live in the IL2CPP helper header; these names only forward to it.
#include "sdk/hook_api.h"
#include "sdk/il2cpp/il2cpp_helpers.h"
#include "sdk/runtime/managed_runtime.h"

namespace URK::managed_hooks {

using DiagnosticSink = URK::il2cpp::helpers::DiagnosticSink;
using URK::il2cpp::helpers::attach_managed;
using URK::il2cpp::helpers::try_hook_method_pointer;
using URK::il2cpp::helpers::try_method_pointer;

// The validated native entry point for a managed method. Mid-function hooks
// attach to this address directly instead of rewriting MethodInfo::methodPointer.
inline void *try_resolve_method_pointer(const URK::managed::Method *method, DiagnosticSink sink = nullptr,
                                        const char *image = nullptr, const char *namespc = nullptr,
                                        const char *klass = nullptr, const char *method_name = nullptr) {
    return URK::il2cpp::helpers::try_method_pointer(method, sink, image, namespc, klass, method_name);
}

} // namespace URK::managed_hooks
