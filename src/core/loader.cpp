#include "loader.h"
#include "config.h"
#include "intro.h"
#include "loader/loader_selection.h"
#include "loader/loader_lifecycle.h"
#include "loader/loader_paths.h"
#include "loader/process_qualification.h"
#include "loader/runtime_backend.h"
#include "logger.h"
#include "mod_sdk.h"

#include <windows.h>

#include <cstddef>
#include <filesystem>
#include <string>

static_assert(offsetof(URK_ModContext, MainThreadRegister) > offsetof(URK_ModContext, mono),
              "URK_ModContext field order changed unexpectedly.");
static_assert(offsetof(URK_ModContext, size) > offsetof(URK_ModContext, MainThreadUnregister),
              "URK_ModContext field order changed unexpectedly.");
static_assert(offsetof(URK_ModContext, gameAssemblyModuleBase) > offsetof(URK_ModContext, runtimeBackendModuleBase),
              "URK_ModContext field order changed unexpectedly.");
static_assert(offsetof(URK_ModContext, network) > offsetof(URK_ModContext, gameAssemblyModuleBase),
              "URK_ModContext network API must remain appended.");

// File-scope Config has std::string/std::vector members whose Debug-STL
// container proxies allocate through operator new. Building it inside DllMain
// (via __dyn_tls_init) risks a loader-lock/CRT-heap-lock deadlock, so it is
// created on first use instead.
static Config &LoaderConfig() {
    static Config config;
    return config;
}

const Config &Config_Current() {
    return LoaderConfig();
}

namespace {
constexpr DWORD kIntroFinalHoldMs = 900;
constexpr unsigned kProcessQualificationTimeoutMs = 30000;
constexpr float kIntroPreparing = 0.04f;
constexpr float kIntroConfigLoaded = 0.10f;
constexpr float kIntroLogReady = 0.14f;
constexpr float kIntroBackendSelected = 0.18f;
constexpr float kIntroComplete = 1.0f;

void IntroStage(float value, const char *status) {
    Intro::Progress(value, status ? std::string(status) : std::string());
}

void LogConfigSummary(const RuntimeBackendDescriptor &backend) {
    Log("=== URKit started (safeMode=%s, runtime=%s) ===", LoaderConfig().safeMode ? "on" : "off", backend.name);
    Log("[config] executableDir='%s', configPath='%s', selectedMods=%zu, initDelayMs=%d, console=%s.",
        Loader_ExeDir().c_str(), LoaderConfig().configPath.c_str(), LoaderConfig().modPaths.size(),
        LoaderConfig().initDelayMs, LoaderConfig().showConsole ? "on" : "off");
}

void DebugSkip(const std::string &reason) {
    const std::string message = "[URKit][loader] bootstrap skipped: " + reason + "\n";
    OutputDebugStringA(message.c_str());
}

class IntroSession {
  public:
    void MarkShown() {
        shown_ = true;
    }
    ~IntroSession() {
        if (shown_)
            Intro::Close();
    }

  private:
    bool shown_ = false;
};
} // namespace

LoaderRunStatus Loader_Run(LoaderStartMode mode) {
    const ProcessQualification qualification = ProcessQualification_WaitForRuntime(kProcessQualificationTimeoutMs);
    if (!qualification.isSupportedRuntime) {
        DebugSkip(qualification.reason);
        return LoaderRunStatus::Skipped;
    }

    if (mode == LoaderStartMode::Injected) {
        LoaderSelection selection;
        if (!Loader_SelectPaths(&selection)) {
            OutputDebugStringA("[URKit][loader] injected session cancelled before runtime startup.\n");
            return LoaderRunStatus::Skipped;
        }

        LoaderConfig() = Config_Load(selection.configPath);
        LoaderConfig().modPaths = selection.modPaths;
    } else {
        LoaderConfig() = Config_Load();
    }

    const std::string logDirectory = mode == LoaderStartMode::Injected
                                         ? std::filesystem::path(LoaderConfig().configPath).parent_path().string()
                                         : std::string();
    Log_Init(LoaderConfig().showConsole, logDirectory);
    const RuntimeBackendDescriptor &backend = RuntimeBackend_Select(LoaderConfig());

    IntroSession introSession;
    Intro::Show("URKit", "Preparing runtime and mods", Loader_GameName(), backend.name, 45, 125, 245, 0);
    introSession.MarkShown();
    IntroStage(kIntroPreparing, "Preparing runtime and mods");
    IntroStage(kIntroConfigLoaded, "Config loaded");

    for (const std::string &warning : LoaderConfig().warnings)
        Log("[config][WARNING] %s", warning.c_str());
    Log("[process] qualification=%s unityPlayer=%s gameAssembly=%s mono=%s unreal=%s.", qualification.reason.c_str(),
        qualification.unityPlayerLoaded ? "loaded" : "not-loaded", qualification.il2cppLoaded ? "loaded" : "not-loaded",
        qualification.monoLoaded ? "loaded" : "not-loaded", qualification.unrealDetected ? "detected" : "not-detected");
    LogConfigSummary(backend);
    IntroStage(kIntroLogReady, "Runtime log ready");

    Intro::Backend(backend.name);
    IntroStage(kIntroBackendSelected, backend.implemented ? "Runtime backend selected" : "Runtime backend reserved");
    const bool backendReady = RuntimeBackend_Run(backend, LoaderConfig());

    Intro::Progress(kIntroComplete);
    if (HANDLE stopEvent = LoaderLifecycle_StopEvent())
        WaitForSingleObject(stopEvent, kIntroFinalHoldMs);
    else
        Sleep(kIntroFinalHoldMs);
    if (backendReady) {
        Log("=== Loader initialization succeeded ===");
        return LoaderRunStatus::Succeeded;
    }
    Log("=== Loader initialization failed; no native mods were started ===");
    return LoaderRunStatus::Failed;
}
