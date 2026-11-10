#pragma once

#include "config.h"
#include "mod_sdk.h"

#include <cstdint>

struct Il2CppApi;

struct ModContextBackendApis {
    const URK_Il2CppApi *il2cpp = nullptr;
};

struct ModContextRuntimeModules {
    uintptr_t backendModuleBase = 0;
    uintptr_t unityPlayerModuleBase = 0;
    uintptr_t gameAssemblyModuleBase = 0;
};

struct ModContextBuildOptions {
    uint32_t runtimeBackend = URK_RUNTIME_BACKEND_UNKNOWN;
    uint64_t backendCapabilities = URK_RUNTIME_CAP_NONE;
    bool mainThreadDispatcherAvailable = false;
    ModContextBackendApis apis{};
    ModContextRuntimeModules modules{};
};

URK_ModContext &ModContext_Build(const Config &config, const ModContextBuildOptions &options);
URK_ModContext &ModContext_BuildIl2Cpp(const Config &config, Il2CppApi &il2cpp, uint64_t backendCapabilities);

