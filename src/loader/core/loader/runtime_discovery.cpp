#include "runtime_discovery.h"

#include <windows.h>

namespace {
bool ModuleLoaded(const char *name) {
    return name && GetModuleHandleA(name) != nullptr;
}
} // namespace

RuntimeModuleSnapshot RuntimeDiscovery_Snapshot() {
    return {
        ModuleLoaded("UnityPlayer.dll"),
        ModuleLoaded("GameAssembly.dll"),
    };
}

const char *RuntimeDiscovery_QualificationReason(const RuntimeModuleSnapshot &snapshot) {
    switch (RuntimeDiscovery_SelectRuntime(snapshot)) {
        case RuntimeModuleKind::Il2Cpp:
            return "GameAssembly.dll loaded";
        case RuntimeModuleKind::None:
            return snapshot.unityPlayerLoaded ? "UnityPlayer.dll loaded"
                                              : "no supported runtime in the current process";
    }
    return "no supported runtime in the current process";
}

const char *RuntimeDiscovery_RuntimeReason(const RuntimeModuleSnapshot &snapshot) {
    switch (RuntimeDiscovery_SelectRuntime(snapshot)) {
        case RuntimeModuleKind::Il2Cpp:
            return "GameAssembly.dll loaded";
        case RuntimeModuleKind::None:
            return "no loaded scripting runtime module";
    }
    return "no loaded scripting runtime module";
}
