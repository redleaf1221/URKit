#pragma once

enum class RuntimeModuleKind {
    None,
    Il2Cpp,
};

struct RuntimeModuleSnapshot {
    bool unityPlayerLoaded = false;
    bool il2cppLoaded = false;

    constexpr bool IsUnityProcess() const noexcept {
        return unityPlayerLoaded || il2cppLoaded;
    }

    constexpr bool IsSupportedProcess() const noexcept {
        return IsUnityProcess();
    }
};

constexpr RuntimeModuleKind RuntimeDiscovery_SelectRuntime(const RuntimeModuleSnapshot &snapshot) noexcept {
    if (snapshot.il2cppLoaded)
        return RuntimeModuleKind::Il2Cpp;
    return RuntimeModuleKind::None;
}

RuntimeModuleSnapshot RuntimeDiscovery_Snapshot();
const char *RuntimeDiscovery_QualificationReason(const RuntimeModuleSnapshot &snapshot);
const char *RuntimeDiscovery_RuntimeReason(const RuntimeModuleSnapshot &snapshot);
