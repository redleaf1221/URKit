#include "runtime_events.h"

#include "config.h"
#include "hooks.h"
#include "cursor_lease_registry.h"
#include "il2cpp_api.h"
#include "loader.h"
#include "logger.h"
#include "main_thread_dispatcher.h"
#include "mod_lifecycle.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <bit>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <new>
#include <string>
#include <type_traits>

namespace {
template <typename FunctionPointer> FunctionPointer FunctionPointerFromAddress(void *address) {
    static_assert(std::is_pointer_v<FunctionPointer>);
    static_assert(std::is_function_v<std::remove_pointer_t<FunctionPointer>>);
    static_assert(sizeof(FunctionPointer) == sizeof(address));
    return std::bit_cast<FunctionPointer>(address);
}







struct Il2CppSceneMethods {
    Il2CppClass *sceneClass = nullptr;
    const Il2CppMethod *getActiveScene = nullptr;
    const Il2CppMethod *internalSceneLoaded = nullptr;
    const Il2CppMethod *internalActiveSceneChanged = nullptr;
    const Il2CppMethod *isValidInternal = nullptr;
    const Il2CppMethod *getNameInternal = nullptr;
    const Il2CppMethod *getBuildIndexInternal = nullptr;
    const Il2CppMethod *getIsValid = nullptr;
    const Il2CppMethod *getName = nullptr;
    const Il2CppMethod *getBuildIndex = nullptr;

    // Scene events only need GetActiveScene. The name and build index are read
    // when the game kept those members; a stripped Scene.get_name or
    // get_buildIndex leaves the field empty/-1 instead of disabling events.
    bool ready() const {
        return getActiveScene != nullptr;
    }
};

struct Il2CppCursorMethods {
    const Il2CppMethod *getVisible = nullptr;
    const Il2CppMethod *setVisible = nullptr;
    const Il2CppMethod *getLockState = nullptr;
    const Il2CppMethod *setLockState = nullptr;

    // Setters are the capability; getters only tell what to restore.
    bool ready() const {
        return setVisible && setLockState;
    }
    bool canReadState() const {
        return getVisible && getLockState;
    }
};

struct Il2CppInputMethods {
    const Il2CppMethod *getKey = nullptr;
    const Il2CppMethod *getKeyDown = nullptr;
    const Il2CppMethod *getKeyUp = nullptr;
    const Il2CppMethod *getMouseButton = nullptr;
    const Il2CppMethod *getMouseButtonDown = nullptr;
    const Il2CppMethod *getMouseButtonUp = nullptr;

    bool ready() const {
        return getKey && getKeyDown && getKeyUp && getMouseButton && getMouseButtonDown && getMouseButtonUp;
    }
    bool readyForMouseSuppression() const {
        return getMouseButton && getMouseButtonDown && getMouseButtonUp;
    }
};

struct Il2CppFrameMethods {
    const Il2CppMethod *invokeOnBeforeRender = nullptr;
    const Il2CppMethod *getGraphicsDeviceType = nullptr;

    bool frame_ready() const {
        return invokeOnBeforeRender != nullptr;
    }
    bool graphics_ready() const {
        return getGraphicsDeviceType != nullptr;
    }
    bool ready() const {
        return frame_ready() && graphics_ready();
    }
};

struct Il2CppApplicationMethods {
    const Il2CppMethod *internalApplicationQuit = nullptr;

    bool ready() const {
        return internalApplicationQuit != nullptr;
    }
};

struct Il2CppObjectDestroyMethods {
    const Il2CppMethod *destroy = nullptr;
    const Il2CppMethod *destroyImmediate = nullptr;
    const Il2CppMethod *getInstanceId = nullptr;
    const Il2CppMethod *getName = nullptr;

    bool hooks_ready() const {
        return destroy && destroyImmediate;
    }
};

// Debug.Log/LogWarning/LogError take a boxed object, so a single signature covers
// both the string overloads and the ones that pass a context object.

struct Il2CppLogMethods {
    const Il2CppMethod *log = nullptr;
    const Il2CppMethod *logWarning = nullptr;
    const Il2CppMethod *logError = nullptr;

    bool ready() const {
        return log && logWarning && logError;
    }
};

enum class RuntimeEventsBackend {
    None,
    Il2Cpp,
    // A non-Unity engine that pumps from its own game thread.
};

struct CursorState {
    bool visible = false;
    int32_t lockState = 0;
};

struct SceneValue {
    int32_t handle = 0;
};

using Il2CppSceneLoadedFn = void (*)(SceneValue scene, int32_t mode, const void *methodInfo);
using Il2CppActiveSceneChangedFn = void (*)(SceneValue previousScene, SceneValue currentScene, const void *methodInfo);
using Il2CppInputBoolFn = bool (*)(int32_t value, const void *methodInfo);
using Il2CppBeforeRenderFn = void (*)(const void *methodInfo);
using Il2CppApplicationQuitFn = void (*)(const void *methodInfo);
using Il2CppDestroyFn = void (*)(Il2CppObject *object, float delaySeconds, const void *methodInfo);
using Il2CppDestroyImmediateFn = void (*)(Il2CppObject *object, bool allowDestroyingAssets, const void *methodInfo);
using Il2CppUnityLogFn = void (*)(Il2CppObject *message, const void *methodInfo);

std::mutex g_eventsMutex;
RuntimeEventsBackend g_backend = RuntimeEventsBackend::None;
Il2CppApi *g_il2cpp = nullptr;
uint64_t g_capabilities = URK_RUNTIME_CAP_NONE;
Il2CppSceneMethods g_il2cppSceneMethods{};
Il2CppCursorMethods g_il2cppCursorMethods{};
Il2CppInputMethods g_il2cppInputMethods{};
Il2CppFrameMethods g_il2cppFrameMethods{};
Il2CppApplicationMethods g_il2cppApplicationMethods{};
Il2CppObjectDestroyMethods g_il2cppObjectDestroyMethods{};
Il2CppSceneLoadedFn g_originalIl2CppSceneLoaded = nullptr;
Il2CppActiveSceneChangedFn g_originalIl2CppActiveSceneChanged = nullptr;
Il2CppInputBoolFn g_originalIl2CppMouseButton = nullptr;
Il2CppInputBoolFn g_originalIl2CppMouseButtonDown = nullptr;
Il2CppInputBoolFn g_originalIl2CppMouseButtonUp = nullptr;
Il2CppBeforeRenderFn g_originalIl2CppBeforeRender = nullptr;
Il2CppApplicationQuitFn g_originalIl2CppApplicationQuit = nullptr;
Il2CppDestroyFn g_originalIl2CppDestroy = nullptr;
Il2CppDestroyImmediateFn g_originalIl2CppDestroyImmediate = nullptr;
Il2CppLogMethods g_il2cppLogMethods{};
Il2CppUnityLogFn g_originalIl2CppUnityLog = nullptr;
Il2CppUnityLogFn g_originalIl2CppUnityLogWarning = nullptr;
Il2CppUnityLogFn g_originalIl2CppUnityLogError = nullptr;

URK_SceneInfo g_currentScene{};
bool g_haveScene = false;
bool g_sceneFailureLogged = false;
bool g_cursorFailureLogged = false;
bool g_inputFailureLogged = false;
bool g_pumpActiveLogged = false;
bool g_cursorOverrideActive = false;
CursorState g_savedCursorState{};
std::atomic<int> g_menuCursorDesired{-1};
CursorLeaseRegistry g_menuCursorLeases;
std::atomic_bool g_menuMouseCaptureDesired{false};
CursorLeaseRegistry g_menuMouseCaptureLeases;
std::atomic<int> g_menuCursorLastApplyResult{-1};
std::atomic<DWORD> g_unityMainThreadId{0};
bool g_mouseInputSuppressionInstalled = false;
bool g_il2cppFrameHookInstallFailed = false;
bool g_il2cppSceneHookInstallFailed = false;
bool g_il2cppMouseHookInstallFailed = false;
bool g_il2cppApplicationQuitHookInstallFailed = false;
bool g_il2cppObjectDestroyHookInstallFailed = false;
bool g_il2cppLogHookInstallFailed = false;
bool g_il2cppFrameResolutionLogged = false;
bool g_il2cppSceneResolutionLogged = false;
bool g_il2cppCursorResolutionLogged = false;
bool g_il2cppInputResolutionLogged = false;
std::atomic_bool g_graphicsThreadFailureLogged{false};
std::atomic_bool g_graphicsInvokeFailureLogged{false};
std::atomic_bool g_applicationQuitObserved{false};
thread_local bool g_runtimePumpActive = false;
std::mutex g_il2cppActivationWorkerMutex;
std::atomic<uint32_t> g_eventsGeneration{1};
HANDLE g_il2cppActivationWorker = nullptr;
DWORD g_il2cppActivationWorkerId = 0;

void Il2CppSceneLoadedDetour(SceneValue scene, int32_t mode, const void *methodInfo);
void Il2CppActiveSceneChangedDetour(SceneValue previousScene, SceneValue currentScene, const void *methodInfo);
bool Il2CppMouseButtonDetour(int32_t value, const void *methodInfo);
bool Il2CppMouseButtonDownDetour(int32_t value, const void *methodInfo);
bool Il2CppMouseButtonUpDetour(int32_t value, const void *methodInfo);
void Il2CppBeforeRenderDetour(const void *methodInfo);
void Il2CppApplicationQuitDetour(const void *methodInfo);
void Il2CppDestroyDetour(Il2CppObject *object, float delaySeconds, const void *methodInfo);
void Il2CppDestroyImmediateDetour(Il2CppObject *object, bool allowDestroyingAssets, const void *methodInfo);
void Il2CppUnityLogDetour(Il2CppObject *message, const void *methodInfo);
void Il2CppUnityLogWarningDetour(Il2CppObject *message, const void *methodInfo);
void Il2CppUnityLogErrorDetour(Il2CppObject *message, const void *methodInfo);
uint64_t TryActivateIl2CppRuntimeEvents(Il2CppApi &il2cpp);
void StartIl2CppActivationWorker(Il2CppApi &il2cpp);
void StopIl2CppActivationWorker();

const char *BoolText(bool value) {
    return value ? "yes" : "no";
}


template <typename SceneMethods>
void LogSceneMethodsUnavailable(const char *backend, const char *message, const SceneMethods &sceneMethods) {
    Log("[runtime][events][%s] %s: "
        "SceneManager.GetActiveScene=%s Scene.IsValidInternal=%s "
        "Scene.GetNameInternal=%s Scene.GetBuildIndexInternal=%s "
        "Scene.get_isValid=%s Scene.get_name=%s Scene.get_buildIndex=%s "
        "SceneManager.Internal_SceneLoaded=%s SceneManager.Internal_ActiveSceneChanged=%s.",
        backend ? backend : "unknown", message ? message : "Scene events unavailable",
        BoolText(sceneMethods.getActiveScene != nullptr), BoolText(sceneMethods.isValidInternal != nullptr),
        BoolText(sceneMethods.getNameInternal != nullptr), BoolText(sceneMethods.getBuildIndexInternal != nullptr),
        BoolText(sceneMethods.getIsValid != nullptr), BoolText(sceneMethods.getName != nullptr),
        BoolText(sceneMethods.getBuildIndex != nullptr), BoolText(sceneMethods.internalSceneLoaded != nullptr),
        BoolText(sceneMethods.internalActiveSceneChanged != nullptr));
}

template <typename CursorMethods>
void LogCursorMethodsUnavailable(const char *backend, const char *message, const CursorMethods &cursorMethods) {
    Log("[runtime][events][%s] %s: "
        "Cursor.get_visible=%s Cursor.set_visible=%s "
        "Cursor.get_lockState=%s Cursor.set_lockState=%s.",
        backend ? backend : "unknown", message ? message : "Cursor control unavailable",
        BoolText(cursorMethods.getVisible != nullptr), BoolText(cursorMethods.setVisible != nullptr),
        BoolText(cursorMethods.getLockState != nullptr), BoolText(cursorMethods.setLockState != nullptr));
}

template <typename InputMethods>
void LogInputMethodsUnavailable(const char *backend, const char *message, const InputMethods &inputMethods) {
    Log("[runtime][events][%s] %s: "
        "Input.GetKey=%s Input.GetKeyDown=%s Input.GetKeyUp=%s "
        "Input.GetMouseButton=%s Input.GetMouseButtonDown=%s "
        "Input.GetMouseButtonUp=%s.",
        backend ? backend : "unknown", message ? message : "Input helpers unavailable",
        BoolText(inputMethods.getKey != nullptr), BoolText(inputMethods.getKeyDown != nullptr),
        BoolText(inputMethods.getKeyUp != nullptr), BoolText(inputMethods.getMouseButton != nullptr),
        BoolText(inputMethods.getMouseButtonDown != nullptr), BoolText(inputMethods.getMouseButtonUp != nullptr));
}

template <typename FrameMethods>
void LogFrameMethodsUnavailable(const char *backend, const char *message, const FrameMethods &frameMethods) {
    Log("[runtime][events][%s][WARNING] %s: "
        "Application.InvokeOnBeforeRender=%s "
        "SystemInfo.get_graphicsDeviceType=%s.",
        backend ? backend : "unknown", message ? message : "Frame pump unavailable",
        BoolText(frameMethods.invokeOnBeforeRender != nullptr),
        BoolText(frameMethods.getGraphicsDeviceType != nullptr));
}

const char *BackendText(RuntimeEventsBackend backend) {
    switch (backend) {
        case RuntimeEventsBackend::Il2Cpp:
            return "IL2CPP";
        default:
            return "None";
    }
}

bool IsExecutableAddress(void *address) {
    if (!address)
        return false;

    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(address, &mbi, sizeof(mbi)))
        return false;
    if (mbi.State != MEM_COMMIT)
        return false;
    if (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS))
        return false;

    const DWORD protection = mbi.Protect & 0xff;
    return protection == PAGE_EXECUTE || protection == PAGE_EXECUTE_READ || protection == PAGE_EXECUTE_READWRITE ||
           protection == PAGE_EXECUTE_WRITECOPY;
}

HMODULE ModuleForAddress(void *address) {
    if (!address)
        return nullptr;
    HMODULE module = nullptr;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCSTR>(address), &module)) {
        return nullptr;
    }
    return module;
}

bool IsUnityPlayerExecutableAddress(void *address) {
    const HMODULE unityPlayer = GetModuleHandleA("UnityPlayer.dll");
    return unityPlayer && IsExecutableAddress(address) && ModuleForAddress(address) == unityPlayer;
}

void CopyText(char *output, size_t outputSize, const char *text) {
    if (!output || outputSize == 0)
        return;
    output[0] = '\0';
    if (!text)
        return;
    const size_t count = (std::min)(std::strlen(text), outputSize - 1);
    std::memcpy(output, text, count);
    output[count] = '\0';
}

class Il2CppRuntimeThreadScope {
  public:
    explicit Il2CppRuntimeThreadScope(Il2CppApi &il2cpp) : il2cpp_(il2cpp) {
        if (!il2cpp_.MetadataAccessReady() || !il2cpp_.thread_attach_available())
            return;

        Il2CppThread *current = il2cpp_.il2cpp_thread_current();
        if (current) {
            thread_ = current;
            attached_ = true;
            ownsAttach_ = false;
            return;
        }

        Il2CppDomain *domain = il2cpp_.Domain();
        if (!domain || !il2cpp_.il2cpp_thread_attach)
            return;

        thread_ = il2cpp_.il2cpp_thread_attach(domain);
        attached_ = thread_ != nullptr;
        ownsAttach_ = attached_;
    }

    ~Il2CppRuntimeThreadScope() {
        if (ownsAttach_ && thread_ && il2cpp_.il2cpp_thread_detach)
            il2cpp_.il2cpp_thread_detach(thread_);
    }

    bool IsAttached() const {
        return attached_;
    }

  private:
    Il2CppApi &il2cpp_;
    Il2CppThread *thread_ = nullptr;
    bool attached_ = false;
    bool ownsAttach_ = false;
};






const Il2CppMethod *FindIl2CppUnityMethod(Il2CppApi &il2cpp, const char *namespc, const char *klass, const char *method,
                                          int argc) {
    const char *images[] = {"UnityEngine.CoreModule.dll", "UnityEngine.dll"};
    for (const char *image : images) {
        Il2CppClass *resolvedClass = il2cpp.FindClass(image, namespc, klass);
        if (!resolvedClass)
            continue;
        if (const Il2CppMethod *resolved = il2cpp.FindMethod(resolvedClass, method, argc))
            return resolved;
    }
    return nullptr;
}

const Il2CppMethod *FindIl2CppUnityMethodExact(Il2CppApi &il2cpp, const char *namespc, const char *klass,
                                               const char *method, const char *const *parameterTypes,
                                               int parameterCount) {
    const char *images[] = {"UnityEngine.CoreModule.dll", "UnityEngine.dll"};
    for (const char *image : images) {
        Il2CppClass *resolvedClass = il2cpp.FindClass(image, namespc, klass);
        if (!resolvedClass)
            continue;
        const Il2CppMethod *resolved = il2cpp.FindMethodExact(resolvedClass, method, parameterTypes, parameterCount);
        if (resolved)
            return resolved;
    }
    return nullptr;
}

const Il2CppMethod *FindIl2CppUnityMethodExactOrArity(Il2CppApi &il2cpp, const char *namespc, const char *klass,
                                                      const char *method, const char *const *parameterTypes,
                                                      int parameterCount) {
    if (const Il2CppMethod *resolved =
            FindIl2CppUnityMethodExact(il2cpp, namespc, klass, method, parameterTypes, parameterCount)) {
        return resolved;
    }
    return FindIl2CppUnityMethod(il2cpp, namespc, klass, method, parameterCount);
}

Il2CppClass *FindIl2CppUnityClass(Il2CppApi &il2cpp, const char *namespc, const char *klass) {
    const char *images[] = {"UnityEngine.CoreModule.dll", "UnityEngine.dll"};
    for (const char *image : images) {
        if (Il2CppClass *resolved = il2cpp.FindClass(image, namespc, klass))
            return resolved;
    }
    return nullptr;
}

const Il2CppMethod *FindIl2CppUnityInputMethodExact(Il2CppApi &il2cpp, const char *method,
                                                    const char *const *parameterTypes, int parameterCount) {
    const char *images[] = {"UnityEngine.InputLegacyModule.dll", "UnityEngine.CoreModule.dll", "UnityEngine.dll"};
    for (const char *image : images) {
        Il2CppClass *inputClass = il2cpp.FindClass(image, "UnityEngine", "Input");
        if (!inputClass)
            continue;
        if (const Il2CppMethod *resolved = il2cpp.FindMethodExact(inputClass, method, parameterTypes, parameterCount)) {
            return resolved;
        }
    }
    return nullptr;
}

Il2CppSceneMethods ResolveIl2CppSceneMethods(Il2CppApi &il2cpp) {
    Il2CppSceneMethods methods{};

    methods.sceneClass = FindIl2CppUnityClass(il2cpp, "UnityEngine.SceneManagement", "Scene");
    methods.getActiveScene = FindIl2CppUnityMethodExactOrArity(il2cpp, "UnityEngine.SceneManagement", "SceneManager",
                                                               "GetActiveScene", nullptr, 0);
    methods.internalSceneLoaded =
        FindIl2CppUnityMethod(il2cpp, "UnityEngine.SceneManagement", "SceneManager", "Internal_SceneLoaded", 2);
    methods.internalActiveSceneChanged =
        FindIl2CppUnityMethod(il2cpp, "UnityEngine.SceneManagement", "SceneManager", "Internal_ActiveSceneChanged", 2);
    const char *int32[] = {"System.Int32"};
    methods.isValidInternal =
        FindIl2CppUnityMethodExactOrArity(il2cpp, "UnityEngine.SceneManagement", "Scene", "IsValidInternal", int32, 1);
    methods.getNameInternal =
        FindIl2CppUnityMethodExactOrArity(il2cpp, "UnityEngine.SceneManagement", "Scene", "GetNameInternal", int32, 1);
    methods.getBuildIndexInternal = FindIl2CppUnityMethodExactOrArity(il2cpp, "UnityEngine.SceneManagement", "Scene",
                                                                      "GetBuildIndexInternal", int32, 1);
    if (il2cpp.il2cpp_value_box) {
        methods.getIsValid = FindIl2CppUnityMethodExactOrArity(il2cpp, "UnityEngine.SceneManagement", "Scene",
                                                               "get_isValid", nullptr, 0);
        methods.getName =
            FindIl2CppUnityMethodExactOrArity(il2cpp, "UnityEngine.SceneManagement", "Scene", "get_name", nullptr, 0);
        methods.getBuildIndex = FindIl2CppUnityMethodExactOrArity(il2cpp, "UnityEngine.SceneManagement", "Scene",
                                                                  "get_buildIndex", nullptr, 0);
    }

    return methods;
}

Il2CppCursorMethods ResolveIl2CppCursorMethods(Il2CppApi &il2cpp) {
    Il2CppCursorMethods methods{};
    const char *boolean[] = {"System.Boolean"};
    const char *cursorLockMode[] = {"UnityEngine.CursorLockMode"};
    methods.getVisible = FindIl2CppUnityMethodExactOrArity(il2cpp, "UnityEngine", "Cursor", "get_visible", nullptr, 0);
    if (!methods.getVisible)
        methods.getVisible =
            FindIl2CppUnityMethodExactOrArity(il2cpp, "UnityEngine", "Cursor", "GetVisible", nullptr, 0);
    methods.setVisible = FindIl2CppUnityMethodExactOrArity(il2cpp, "UnityEngine", "Cursor", "set_visible", boolean, 1);
    if (!methods.setVisible)
        methods.setVisible =
            FindIl2CppUnityMethodExactOrArity(il2cpp, "UnityEngine", "Cursor", "SetVisible", boolean, 1);
    methods.getLockState =
        FindIl2CppUnityMethodExactOrArity(il2cpp, "UnityEngine", "Cursor", "get_lockState", nullptr, 0);
    if (!methods.getLockState)
        methods.getLockState =
            FindIl2CppUnityMethodExactOrArity(il2cpp, "UnityEngine", "Cursor", "GetLockState", nullptr, 0);
    methods.setLockState =
        FindIl2CppUnityMethodExactOrArity(il2cpp, "UnityEngine", "Cursor", "set_lockState", cursorLockMode, 1);
    if (!methods.setLockState)
        methods.setLockState =
            FindIl2CppUnityMethodExactOrArity(il2cpp, "UnityEngine", "Cursor", "SetLockState", cursorLockMode, 1);
    return methods;
}

Il2CppInputMethods ResolveIl2CppInputMethods(Il2CppApi &il2cpp) {
    Il2CppInputMethods methods{};
    const char *keyCode[] = {"UnityEngine.KeyCode"};
    const char *int32[] = {"System.Int32"};
    methods.getKey = FindIl2CppUnityInputMethodExact(il2cpp, "GetKey", keyCode, 1);
    methods.getKeyDown = FindIl2CppUnityInputMethodExact(il2cpp, "GetKeyDown", keyCode, 1);
    methods.getKeyUp = FindIl2CppUnityInputMethodExact(il2cpp, "GetKeyUp", keyCode, 1);
    methods.getMouseButton = FindIl2CppUnityInputMethodExact(il2cpp, "GetMouseButton", int32, 1);
    methods.getMouseButtonDown = FindIl2CppUnityInputMethodExact(il2cpp, "GetMouseButtonDown", int32, 1);
    methods.getMouseButtonUp = FindIl2CppUnityInputMethodExact(il2cpp, "GetMouseButtonUp", int32, 1);
    return methods;
}

Il2CppFrameMethods ResolveIl2CppFrameMethods(Il2CppApi &il2cpp) {
    Il2CppFrameMethods methods{};
    methods.invokeOnBeforeRender =
        FindIl2CppUnityMethodExact(il2cpp, "UnityEngine", "Application", "InvokeOnBeforeRender", nullptr, 0);
    methods.getGraphicsDeviceType =
        FindIl2CppUnityMethodExact(il2cpp, "UnityEngine", "SystemInfo", "get_graphicsDeviceType", nullptr, 0);
    return methods;
}


Il2CppLogMethods ResolveIl2CppLogMethods(Il2CppApi &il2cpp) {
    Il2CppLogMethods methods{};
    const char *boxedObject[] = {"System.Object"};
    methods.log = FindIl2CppUnityMethodExact(il2cpp, "UnityEngine", "Debug", "Log", boxedObject, 1);
    methods.logWarning = FindIl2CppUnityMethodExact(il2cpp, "UnityEngine", "Debug", "LogWarning", boxedObject, 1);
    methods.logError = FindIl2CppUnityMethodExact(il2cpp, "UnityEngine", "Debug", "LogError", boxedObject, 1);
    return methods;
}

Il2CppApplicationMethods ResolveIl2CppApplicationMethods(Il2CppApi &il2cpp) {
    Il2CppApplicationMethods methods{};
    methods.internalApplicationQuit =
        FindIl2CppUnityMethodExact(il2cpp, "UnityEngine", "Application", "Internal_ApplicationQuit", nullptr, 0);
    return methods;
}

Il2CppObjectDestroyMethods ResolveIl2CppObjectDestroyMethods(Il2CppApi &il2cpp) {
    Il2CppObjectDestroyMethods methods{};
    const char *objectAndSingle[] = {"UnityEngine.Object", "System.Single"};
    const char *objectAndBoolean[] = {"UnityEngine.Object", "System.Boolean"};
    methods.destroy = FindIl2CppUnityMethodExact(il2cpp, "UnityEngine", "Object", "Destroy", objectAndSingle, 2);
    methods.destroyImmediate =
        FindIl2CppUnityMethodExact(il2cpp, "UnityEngine", "Object", "DestroyImmediate", objectAndBoolean, 2);
    methods.getInstanceId = FindIl2CppUnityMethodExact(il2cpp, "UnityEngine", "Object", "GetInstanceID", nullptr, 0);
    methods.getName = FindIl2CppUnityMethodExact(il2cpp, "UnityEngine", "Object", "get_name", nullptr, 0);
    return methods;
}


Il2CppObject *InvokeIl2Cpp(const Il2CppMethod *method, void *object, void **params, const char *label, bool *ok) {
    if (ok)
        *ok = false;
    if (!g_il2cpp || !method || !g_il2cpp->il2cpp_runtime_invoke)
        return nullptr;

    Il2CppObject *managedException = nullptr;
    Il2CppObject *result = g_il2cpp->il2cpp_runtime_invoke(method, object, params, &managedException);

    if (managedException) {
        if (g_il2cpp->il2cpp_format_exception) {
            char message[1024]{};
            g_il2cpp->il2cpp_format_exception(managedException, message, static_cast<int>(sizeof(message)));
            Log("[runtime][events][IL2CPP] %s failed: managedException=%p message=%s.",
                label ? label : "managed invoke", managedException, message);
        } else {
            Log("[runtime][events][IL2CPP] %s failed: managedException=%p.", label ? label : "managed invoke",
                managedException);
        }
        return nullptr;
    }

    if (ok)
        *ok = true;
    return result;
}

bool ReadIl2CppBoxedInt32(Il2CppObject *object, int32_t *value) {
    if (!g_il2cpp || !g_il2cpp->il2cpp_object_unbox || !object || !value)
        return false;
    void *slot = g_il2cpp->il2cpp_object_unbox(object);
    if (!slot)
        return false;
    *value = *static_cast<int32_t *>(slot);
    return true;
}

bool ReadIl2CppBoxedBool(Il2CppObject *object, bool *value) {
    if (!g_il2cpp || !g_il2cpp->il2cpp_object_unbox || !object || !value)
        return false;
    void *slot = g_il2cpp->il2cpp_object_unbox(object);
    if (!slot)
        return false;
    *value = *static_cast<unsigned char *>(slot) != 0;
    return true;
}

std::string Il2CppStringToUtf8(Il2CppObject *object) {
    if (!g_il2cpp || !object || !g_il2cpp->il2cpp_string_length || !g_il2cpp->il2cpp_string_chars)
        return {};

    auto *string = static_cast<Il2CppString *>(object);
    const int32_t length = g_il2cpp->il2cpp_string_length(string);
    const uint16_t *chars = g_il2cpp->il2cpp_string_chars(string);
    if (!chars || length <= 0)
        return {};

    const int required =
        WideCharToMultiByte(CP_UTF8, 0, reinterpret_cast<LPCWCH>(chars), length, nullptr, 0, nullptr, nullptr);
    if (required <= 0)
        return {};

    std::string text(static_cast<size_t>(required), '\0');
    WideCharToMultiByte(CP_UTF8, 0, reinterpret_cast<LPCWCH>(chars), length, text.data(), required, nullptr, nullptr);
    return text;
}




// Unity hands Debug.Log a boxed object; only System.String carries text, so
// everything else degrades to a type name instead of an unreadable handle.
std::string Il2CppLogMessageText(Il2CppObject *message) {
    if (!message)
        return "<null>";
    if (!g_il2cpp)
        return "<unreadable Unity log message>";

    const char *namespc = nullptr;
    const char *name = nullptr;
    if (g_il2cpp->il2cpp_object_get_class && g_il2cpp->il2cpp_class_get_name) {
        Il2CppClass *klass = g_il2cpp->il2cpp_object_get_class(message);
        if (klass) {
            name = g_il2cpp->il2cpp_class_get_name(klass);
            namespc = g_il2cpp->il2cpp_class_get_namespace ? g_il2cpp->il2cpp_class_get_namespace(klass) : nullptr;
        }
    }

    if (name && std::strcmp(name, "String") == 0 && (!namespc || std::strcmp(namespc, "System") == 0)) {
        std::string text = Il2CppStringToUtf8(message);
        if (!text.empty())
            return text;
    }

    char fallback[192]{};
    std::snprintf(fallback, sizeof(fallback), "<%s%s%s object at %p>", namespc && namespc[0] ? namespc : "",
                  namespc && namespc[0] ? "." : "", name && name[0] ? name : "unknown", static_cast<void *>(message));
    return fallback;
}


// A Unity message can carry newlines; one record per line keeps the log parseable.
void EmitUnityLog(const char *level, const std::string &text) {
    std::size_t start = 0;
    for (;;) {
        const std::size_t end = text.find('\n', start);
        const std::string line = end == std::string::npos ? text.substr(start) : text.substr(start, end - start);
        if (!line.empty())
            Log("[%s] [Unity] %s", level, line.c_str());
        if (end == std::string::npos)
            break;
        start = end + 1;
    }
}

void FillObjectDestroyRequest(URK_ObjectDestroyRequest *request, uintptr_t object, int32_t instanceId,
                              float delaySeconds, uint32_t flags, const std::string &name,
                              const std::string &typeName) {
    *request = {};
    request->size = sizeof(URK_ObjectDestroyRequest);
    request->flags = flags;
    request->objectAddress = object;
    request->instanceId = instanceId;
    request->delaySeconds = delaySeconds;
    CopyText(request->name, sizeof(request->name), name.c_str());
    CopyText(request->typeName, sizeof(request->typeName), typeName.c_str());
}


URK_ObjectDestroyRequest CaptureIl2CppObjectDestroyRequest(Il2CppObject *object, float delaySeconds, uint32_t flags) {
    int32_t instanceId = 0;
    std::string name;
    std::string typeName;

    if (object && g_il2cpp) {
        if (g_il2cpp->il2cpp_object_get_class && g_il2cpp->il2cpp_class_get_name) {
            Il2CppClass *klass = g_il2cpp->il2cpp_object_get_class(object);
            const char *className = klass ? g_il2cpp->il2cpp_class_get_name(klass) : nullptr;
            if (className)
                typeName = className;
        }
        bool ok = false;
        if (g_il2cppObjectDestroyMethods.getInstanceId) {
            Il2CppObject *result =
                InvokeIl2Cpp(g_il2cppObjectDestroyMethods.getInstanceId, object, nullptr, "Object.GetInstanceID", &ok);
            if (!ok || !ReadIl2CppBoxedInt32(result, &instanceId))
                instanceId = 0;
        }
        if (g_il2cppObjectDestroyMethods.getName) {
            Il2CppObject *result =
                InvokeIl2Cpp(g_il2cppObjectDestroyMethods.getName, object, nullptr, "Object.get_name", &ok);
            if (ok)
                name = Il2CppStringToUtf8(result);
        }
    }

    URK_ObjectDestroyRequest request{};
    FillObjectDestroyRequest(&request, reinterpret_cast<uintptr_t>(object), instanceId, delaySeconds, flags, name,
                             typeName);
    return request;
}







void FillSceneInfo(URK_SceneInfo *scene, int32_t handle, int32_t buildIndex, const std::string &name) {
    *scene = {};
    scene->size = sizeof(URK_SceneInfo);
    scene->buildIndex = buildIndex;
    scene->handle = handle;
    CopyText(scene->name, sizeof(scene->name), name.c_str());
}



bool PopulateIl2CppSceneFromHandle(int32_t handle, URK_SceneInfo *scene, std::string *reason,
                                   const char *invalidHandleReason, const char *invalidSceneReason) {
    if (!scene || !g_il2cpp) {
        if (reason)
            *reason = "IL2CPP runtime is unavailable";
        return false;
    }
    if (handle == 0) {
        if (reason)
            *reason = invalidHandleReason ? invalidHandleReason : "scene handle is invalid";
        return false;
    }

    // A boxed Scene value is only needed by the instance getters; a game that
    // kept the internal (handle-only) variants never needs one.
    const bool needBoxedScene =
        (!g_il2cppSceneMethods.getNameInternal && g_il2cppSceneMethods.getName) ||
        (!g_il2cppSceneMethods.isValidInternal && g_il2cppSceneMethods.getIsValid) ||
        (!g_il2cppSceneMethods.getBuildIndexInternal && g_il2cppSceneMethods.getBuildIndex);
    Il2CppObject *boxedScene = nullptr;
    if (needBoxedScene) {
        SceneValue value{handle};
        boxedScene = g_il2cppSceneMethods.sceneClass && g_il2cpp->il2cpp_value_box
                         ? g_il2cpp->il2cpp_value_box(g_il2cppSceneMethods.sceneClass, &value)
                         : nullptr;
        if (!boxedScene) {
            if (reason)
                *reason = "IL2CPP could not box the Scene value for property fallback access";
            return false;
        }
    }

    bool ok = true;
    if (g_il2cppSceneMethods.isValidInternal) {
        void *validParams[] = {&handle};
        Il2CppObject *validObject =
            InvokeIl2Cpp(g_il2cppSceneMethods.isValidInternal, nullptr, validParams, "Scene.IsValidInternal", &ok);

        bool valid = false;
        if (!ok || !ReadIl2CppBoxedBool(validObject, &valid) || !valid) {
            if (reason)
                *reason = invalidSceneReason ? invalidSceneReason : "Scene.IsValidInternal rejected the scene handle";
            return false;
        }
    } else if (g_il2cppSceneMethods.getIsValid) {
        Il2CppObject *validObject =
            InvokeIl2Cpp(g_il2cppSceneMethods.getIsValid, boxedScene, nullptr, "Scene.get_isValid", &ok);
        bool valid = false;
        if (!ok || !ReadIl2CppBoxedBool(validObject, &valid) || !valid) {
            if (reason)
                *reason = invalidSceneReason ? invalidSceneReason : "Scene.get_isValid rejected the scene";
            return false;
        }
    }

    void *params[] = {&handle};

    std::string name;
    if (g_il2cppSceneMethods.getNameInternal || g_il2cppSceneMethods.getName) {
        Il2CppObject *nameObject =
            g_il2cppSceneMethods.getNameInternal
                ? InvokeIl2Cpp(g_il2cppSceneMethods.getNameInternal, nullptr, params, "Scene.GetNameInternal", &ok)
                : InvokeIl2Cpp(g_il2cppSceneMethods.getName, boxedScene, nullptr, "Scene.get_name", &ok);
        if (!ok) {
            if (reason)
                *reason = "Scene name lookup failed";
            return false;
        }
        name = Il2CppStringToUtf8(nameObject);
    }

    int32_t buildIndex = -1;
    if (g_il2cppSceneMethods.getBuildIndexInternal || g_il2cppSceneMethods.getBuildIndex) {
        Il2CppObject *buildIndexObject =
            g_il2cppSceneMethods.getBuildIndexInternal
                ? InvokeIl2Cpp(g_il2cppSceneMethods.getBuildIndexInternal, nullptr, params, "Scene.GetBuildIndexInternal",
                               &ok)
                : InvokeIl2Cpp(g_il2cppSceneMethods.getBuildIndex, boxedScene, nullptr, "Scene.get_buildIndex", &ok);
        if (!ok || !ReadIl2CppBoxedInt32(buildIndexObject, &buildIndex)) {
            if (reason)
                *reason = "Scene build index lookup failed";
            return false;
        }
    }

    FillSceneInfo(scene, handle, buildIndex, name);
    return true;
}

bool ReadIl2CppActiveScene(URK_SceneInfo *scene, std::string *reason) {
    if (!scene || !g_il2cpp || !g_il2cppSceneMethods.ready()) {
        if (reason)
            *reason = "IL2CPP scene methods are incomplete";
        return false;
    }

    bool ok = false;
    Il2CppObject *sceneObject =
        InvokeIl2Cpp(g_il2cppSceneMethods.getActiveScene, nullptr, nullptr, "SceneManager.GetActiveScene", &ok);
    if (!ok || !sceneObject) {
        if (reason)
            *reason = "SceneManager.GetActiveScene failed or returned null";
        return false;
    }
    if (!g_il2cpp->il2cpp_object_unbox) {
        if (reason)
            *reason = "il2cpp_object_unbox is unavailable for the Scene value returned by SceneManager.GetActiveScene";
        return false;
    }
    void *sceneValue = g_il2cpp->il2cpp_object_unbox(sceneObject);
    if (!sceneValue) {
        if (reason)
            *reason = "il2cpp_object_unbox failed for the Scene value returned by SceneManager.GetActiveScene";
        return false;
    }
    int32_t handle = 0;
    std::memcpy(&handle, sceneValue, sizeof(handle));

    if (handle == 0) {
        if (reason)
            *reason = "SceneManager.GetActiveScene returned an invalid scene handle";
        return false;
    }

    return PopulateIl2CppSceneFromHandle(handle, scene, reason,
                                         "SceneManager.GetActiveScene returned an invalid scene handle",
                                         "Scene.IsValidInternal rejected the active scene handle");
}

bool ReadActiveScene(URK_SceneInfo *scene, std::string *reason) {
    switch (g_backend) {
        case RuntimeEventsBackend::Il2Cpp:
            return ReadIl2CppActiveScene(scene, reason);
        default:
            if (reason)
                *reason = "runtime events backend is not configured";
            return false;
    }
}

bool ReadSceneByHandle(int32_t handle, URK_SceneInfo *scene, std::string *reason) {
    switch (g_backend) {
        case RuntimeEventsBackend::Il2Cpp:
            return PopulateIl2CppSceneFromHandle(handle, scene, reason, "scene handle is invalid",
                                                 "Scene.IsValidInternal rejected the scene handle");
        default:
            if (reason)
                *reason = "runtime events backend is not configured";
            return false;
    }
}

bool SameScene(const URK_SceneInfo &left, const URK_SceneInfo &right) {
    return left.buildIndex == right.buildIndex && left.handle == right.handle &&
           std::strncmp(left.name, right.name, sizeof(left.name)) == 0;
}

struct SceneTransition {
    URK_SceneInfo previous{};
    bool firstScene = false;
    bool changed = false;
    bool duplicate = false;
};

SceneTransition UpdateObservedScene(const URK_SceneInfo &current, bool markDuplicate = false) {
    SceneTransition transition{};
    std::lock_guard lock(g_eventsMutex);
    if (!g_haveScene) {
        g_currentScene = current;
        g_haveScene = true;
        transition.firstScene = true;
    } else if (!SameScene(g_currentScene, current)) {
        transition.previous = g_currentScene;
        g_currentScene = current;
        transition.changed = true;
    } else {
        transition.duplicate = markDuplicate;
    }
    return transition;
}

void DispatchTransition(const URK_SceneInfo &current, const SceneTransition &transition) {
    if (transition.firstScene) {
        Log("[runtime][events] scene loaded: name='%s' buildIndex=%d handle=%d.", current.name, current.buildIndex,
            current.handle);
        ModLifecycle_DispatchSceneLoaded(current);
    } else if (transition.changed) {
        Log("[runtime][events] scene changed: '%s'(%d/%d) -> '%s'(%d/%d).", transition.previous.name,
            transition.previous.buildIndex, transition.previous.handle, current.name, current.buildIndex,
            current.handle);
        ModLifecycle_DispatchSceneChanged(transition.previous, current);
        ModLifecycle_DispatchSceneLoaded(current);
    }
}

void PumpSceneEvents() {
    URK_SceneInfo current{};
    std::string reason;
    if (!ReadActiveScene(&current, &reason)) {
        if (!g_sceneFailureLogged) {
            Log("[runtime][events][%s][WARNING] Scene event pump could not read "
                "UnityEngine.SceneManagement.SceneManager.GetActiveScene: %s.",
                BackendText(g_backend), reason.empty() ? "unknown failure" : reason.c_str());
            g_sceneFailureLogged = true;
        }
        return;
    }

    DispatchTransition(current, UpdateObservedScene(current));
}

void DispatchSceneLoadedFromHandle(int32_t handle, const char *source) {
    if (ModLifecycle_ShutdownStarted())
        return;

    URK_SceneInfo current{};
    std::string reason;
    if (!ReadSceneByHandle(handle, &current, &reason)) {
        if (!g_sceneFailureLogged) {
            Log("[runtime][events][%s][WARNING] %s could not read scene handle=%d: %s.", BackendText(g_backend),
                source ? source : "scene hook", handle, reason.empty() ? "unknown failure" : reason.c_str());
            g_sceneFailureLogged = true;
        }
        return;
    }

    const SceneTransition transition = UpdateObservedScene(current, true);
    if (transition.duplicate)
        return;

    if (transition.changed) {
        Log("[runtime][events] scene changed: '%s'(%d/%d) -> '%s'(%d/%d).", transition.previous.name,
            transition.previous.buildIndex, transition.previous.handle, current.name, current.buildIndex,
            current.handle);
        ModLifecycle_DispatchSceneChanged(transition.previous, current);
    }
    Log("[runtime][events] scene loaded: name='%s' buildIndex=%d handle=%d.", current.name, current.buildIndex,
        current.handle);
    ModLifecycle_DispatchSceneLoaded(current);
}

void DispatchActiveSceneChangedFromHandles(int32_t previousHandle, int32_t currentHandle, const char *source) {
    if (ModLifecycle_ShutdownStarted())
        return;

    URK_SceneInfo previous{};
    URK_SceneInfo current{};
    std::string reason;
    if (!ReadSceneByHandle(currentHandle, &current, &reason)) {
        if (!g_sceneFailureLogged) {
            Log("[runtime][events][%s][WARNING] %s could not read current scene handle=%d: %s.", BackendText(g_backend),
                source ? source : "scene hook", currentHandle, reason.empty() ? "unknown failure" : reason.c_str());
            g_sceneFailureLogged = true;
        }
        return;
    }
    const bool havePrevious = ReadSceneByHandle(previousHandle, &previous, &reason);
    bool changed = false;
    {
        std::lock_guard lock(g_eventsMutex);
        if (!g_haveScene) {
            g_currentScene = current;
            g_haveScene = true;
            changed = havePrevious && !SameScene(previous, current);
        } else if (!SameScene(g_currentScene, current)) {
            if (!havePrevious)
                previous = g_currentScene;
            g_currentScene = current;
            changed = true;
        }
    }
    if (!changed)
        return;
    Log("[runtime][events] scene changed: '%s'(%d/%d) -> '%s'(%d/%d).", previous.name, previous.buildIndex,
        previous.handle, current.name, current.buildIndex, current.handle);
    ModLifecycle_DispatchSceneChanged(previous, current);
}

void DispatchActiveSceneChangedFromActiveScene(const char *source) {
    if (ModLifecycle_ShutdownStarted())
        return;

    URK_SceneInfo current{};
    std::string reason;
    if (!ReadActiveScene(&current, &reason)) {
        if (!g_sceneFailureLogged) {
            Log("[runtime][events][%s][WARNING] %s could not read current "
                "active scene: %s.",
                BackendText(g_backend), source ? source : "scene hook",
                reason.empty() ? "unknown failure" : reason.c_str());
            g_sceneFailureLogged = true;
        }
        return;
    }

    const SceneTransition transition = UpdateObservedScene(current);

    if (transition.firstScene) {
        Log("[runtime][events] scene loaded: name='%s' buildIndex=%d handle=%d.", current.name, current.buildIndex,
            current.handle);
        ModLifecycle_DispatchSceneLoaded(current);
        return;
    }

    if (!transition.changed)
        return;

    Log("[runtime][events] scene changed: '%s'(%d/%d) -> '%s'(%d/%d).", transition.previous.name,
        transition.previous.buildIndex, transition.previous.handle, current.name, current.buildIndex, current.handle);
    ModLifecycle_DispatchSceneChanged(transition.previous, current);
    ModLifecycle_DispatchSceneLoaded(current);
}



void Il2CppSceneLoadedDetour(SceneValue scene, int32_t mode, const void *methodInfo) {
    if (!g_originalIl2CppSceneLoaded)
        return;
    g_originalIl2CppSceneLoaded(scene, mode, methodInfo);
    Il2CppRuntimeThreadScope scope(*g_il2cpp);
    if (scope.IsAttached())
        DispatchSceneLoadedFromHandle(scene.handle, "SceneManager.Internal_SceneLoaded");
}

void Il2CppActiveSceneChangedDetour(SceneValue previousScene, SceneValue currentScene, const void *methodInfo) {
    if (!g_originalIl2CppActiveSceneChanged)
        return;
    g_originalIl2CppActiveSceneChanged(previousScene, currentScene, methodInfo);
    Il2CppRuntimeThreadScope scope(*g_il2cpp);
    if (scope.IsAttached())
        DispatchActiveSceneChangedFromHandles(previousScene.handle, currentScene.handle,
                                              "SceneManager.Internal_ActiveSceneChanged");
}


void Il2CppBeforeRenderDetour(const void *methodInfo) {
    g_unityMainThreadId.store(GetCurrentThreadId(), std::memory_order_release);
    if (!g_originalIl2CppBeforeRender)
        return;

    g_originalIl2CppBeforeRender(methodInfo);
    RuntimeEvents_Pump();
}




void Il2CppUnityLogDetour(Il2CppObject *message, const void *methodInfo) {
    EmitUnityLog("info", Il2CppLogMessageText(message));
    if (g_originalIl2CppUnityLog)
        g_originalIl2CppUnityLog(message, methodInfo);
}

void Il2CppUnityLogWarningDetour(Il2CppObject *message, const void *methodInfo) {
    EmitUnityLog("warning", Il2CppLogMessageText(message));
    if (g_originalIl2CppUnityLogWarning)
        g_originalIl2CppUnityLogWarning(message, methodInfo);
}

void Il2CppUnityLogErrorDetour(Il2CppObject *message, const void *methodInfo) {
    EmitUnityLog("error", Il2CppLogMessageText(message));
    if (g_originalIl2CppUnityLogError)
        g_originalIl2CppUnityLogError(message, methodInfo);
}

void DispatchApplicationQuit(const char *backend) {
    if (!g_applicationQuitObserved.exchange(true, std::memory_order_acq_rel)) {
        Log("[runtime][events][%s] Application quit observed; starting loader shutdown.",
            backend ? backend : "unknown");
        Loader_Shutdown();
    }
}


void Il2CppApplicationQuitDetour(const void *methodInfo) {
    DispatchApplicationQuit("IL2CPP");
    if (g_originalIl2CppApplicationQuit)
        g_originalIl2CppApplicationQuit(methodInfo);
}



void Il2CppDestroyDetour(Il2CppObject *object, float delaySeconds, const void *methodInfo) {
    const URK_ObjectDestroyRequest request =
        CaptureIl2CppObjectDestroyRequest(object, delaySeconds, URK_OBJECT_DESTROY_REQUEST_NONE);
    if (g_originalIl2CppDestroy)
        g_originalIl2CppDestroy(object, delaySeconds, methodInfo);
    ModLifecycle_DispatchObjectDestroyRequested(request);
}

void Il2CppDestroyImmediateDetour(Il2CppObject *object, bool allowDestroyingAssets, const void *methodInfo) {
    const uint32_t flags = URK_OBJECT_DESTROY_REQUEST_IMMEDIATE |
                           (allowDestroyingAssets ? URK_OBJECT_DESTROY_REQUEST_ALLOW_DESTROYING_ASSETS : 0u);
    const URK_ObjectDestroyRequest request = CaptureIl2CppObjectDestroyRequest(object, 0.0f, flags);
    if (g_originalIl2CppDestroyImmediate)
        g_originalIl2CppDestroyImmediate(object, allowDestroyingAssets, methodInfo);
    ModLifecycle_DispatchObjectDestroyRequested(request);
}

// Cursor state as Windows sees it, for builds with stripped Cursor getters.
bool ReadNativeCursorState(CursorState *state) {
    if (!state)
        return false;
    CURSORINFO info{};
    info.cbSize = sizeof(info);
    state->visible = GetCursorInfo(&info) != 0 && (info.flags & CURSOR_SHOWING) != 0;
    RECT clip{};
    state->lockState = URK_CURSOR_LOCK_NONE;
    if (GetClipCursor(&clip) != 0) {
        const int screenWidth = GetSystemMetrics(SM_CXVIRTUALSCREEN);
        const int screenHeight = GetSystemMetrics(SM_CYVIRTUALSCREEN);
        const bool clipped = (clip.right - clip.left) < screenWidth || (clip.bottom - clip.top) < screenHeight;
        if (clipped)
            state->lockState = state->visible ? URK_CURSOR_LOCK_CONFINED : URK_CURSOR_LOCK_LOCKED;
    }
    return true;
}




bool ReadIl2CppCursorState(CursorState *state) {
    if (!state || !g_il2cpp || !g_il2cppCursorMethods.ready())
        return false;
    if (!g_il2cppCursorMethods.canReadState())
        return ReadNativeCursorState(state);

    bool ok = false;
    Il2CppObject *visibleObject =
        InvokeIl2Cpp(g_il2cppCursorMethods.getVisible, nullptr, nullptr, "Cursor.get_visible", &ok);
    if (!ok || !ReadIl2CppBoxedBool(visibleObject, &state->visible))
        return false;

    Il2CppObject *lockObject =
        InvokeIl2Cpp(g_il2cppCursorMethods.getLockState, nullptr, nullptr, "Cursor.get_lockState", &ok);
    return ok && ReadIl2CppBoxedInt32(lockObject, &state->lockState);
}

bool SetIl2CppCursorVisible(bool visible) {
    bool value = visible;
    void *params[] = {&value};
    bool ok = false;
    InvokeIl2Cpp(g_il2cppCursorMethods.setVisible, nullptr, params, "Cursor.set_visible", &ok);
    return ok;
}

bool SetIl2CppCursorLockState(int32_t lockState) {
    void *params[] = {&lockState};
    bool ok = false;
    InvokeIl2Cpp(g_il2cppCursorMethods.setLockState, nullptr, params, "Cursor.set_lockState", &ok);
    return ok;
}


bool InvokeIl2CppInputBool(const Il2CppMethod *method, int32_t value) {
    if (!method)
        return false;
    void *params[] = {&value};
    bool ok = false;
    Il2CppObject *result = InvokeIl2Cpp(method, nullptr, params, "Input managed invoke", &ok);
    bool state = false;
    return ok && ReadIl2CppBoxedBool(result, &state) && state;
}


bool ReadIl2CppGraphicsDeviceType(int32_t *value) {
    if (!value || !g_il2cppFrameMethods.getGraphicsDeviceType)
        return false;
    bool ok = false;
    Il2CppObject *result = InvokeIl2Cpp(g_il2cppFrameMethods.getGraphicsDeviceType, nullptr, nullptr,
                                        "SystemInfo.get_graphicsDeviceType", &ok);
    return ok && ReadIl2CppBoxedInt32(result, value);
}

bool MouseInputBlocked() {
    // Showing the cursor does not block mouse input by itself.
    return g_menuMouseCaptureDesired.load(std::memory_order_acquire);
}

bool Il2CppMouseButtonDetour(int32_t value, const void *methodInfo) {
    return MouseInputBlocked() || !g_originalIl2CppMouseButton ? false : g_originalIl2CppMouseButton(value, methodInfo);
}
bool Il2CppMouseButtonDownDetour(int32_t value, const void *methodInfo) {
    return MouseInputBlocked() || !g_originalIl2CppMouseButtonDown ? false
                                                                   : g_originalIl2CppMouseButtonDown(value, methodInfo);
}
bool Il2CppMouseButtonUpDetour(int32_t value, const void *methodInfo) {
    return MouseInputBlocked() || !g_originalIl2CppMouseButtonUp ? false
                                                                 : g_originalIl2CppMouseButtonUp(value, methodInfo);
}

bool ReadInputStateByKind(int32_t value, int kind) {
    switch (g_backend) {
        case RuntimeEventsBackend::Il2Cpp:
            switch (kind) {
                case 0:
                    return InvokeIl2CppInputBool(g_il2cppInputMethods.getKey, value);
                case 1:
                    return InvokeIl2CppInputBool(g_il2cppInputMethods.getKeyDown, value);
                case 2:
                    return InvokeIl2CppInputBool(g_il2cppInputMethods.getKeyUp, value);
                case 3:
                    return InvokeIl2CppInputBool(g_il2cppInputMethods.getMouseButton, value);
                case 4:
                    return InvokeIl2CppInputBool(g_il2cppInputMethods.getMouseButtonDown, value);
                case 5:
                    return InvokeIl2CppInputBool(g_il2cppInputMethods.getMouseButtonUp, value);
                default:
                    return false;
            }
        default:
            return false;
    }
}

bool ReadCursorState(CursorState *state) {
    switch (g_backend) {
        case RuntimeEventsBackend::Il2Cpp:
            return ReadIl2CppCursorState(state);
        default:
            return false;
    }
}

bool SetCursorVisible(bool visible) {
    switch (g_backend) {
        case RuntimeEventsBackend::Il2Cpp:
            return SetIl2CppCursorVisible(visible);
        default:
            return false;
    }
}

bool SetCursorLockState(int32_t lockState) {
    switch (g_backend) {
        case RuntimeEventsBackend::Il2Cpp:
            return SetIl2CppCursorLockState(lockState);
        default:
            return false;
    }
}

bool ApplyCursorState(const CursorState &state) {
    const bool visibleOk = SetCursorVisible(state.visible);
    const bool lockOk = SetCursorLockState(state.lockState);
    return visibleOk && lockOk;
}

void RestoreCursorOverrideForResetLocked() {
    if (!g_cursorOverrideActive)
        return;

    bool restored = false;
    switch (g_backend) {
        case RuntimeEventsBackend::Il2Cpp: {
            if (!g_il2cpp)
                break;
            Il2CppRuntimeThreadScope scope(*g_il2cpp);
            restored = scope.IsAttached() && ApplyCursorState(g_savedCursorState);
            break;
        }
        default:
            break;
    }

    if (restored) {
        Log("[runtime][events] Unity cursor override reset; restored visible=%s "
            "lockState=%d.",
            BoolText(g_savedCursorState.visible), g_savedCursorState.lockState);
    } else {
        Log("[runtime][events][%s][WARNING] Unity cursor override reset failed; "
            "saved Cursor.visible/lockState could not be restored.",
            BackendText(g_backend));
    }
}

void ResetStateLocked() {
    RestoreCursorOverrideForResetLocked();

    if (g_originalIl2CppBeforeRender)
        Hook_Detach(reinterpret_cast<void **>(&g_originalIl2CppBeforeRender),
                    reinterpret_cast<void *>(&Il2CppBeforeRenderDetour));
    if (g_originalIl2CppApplicationQuit)
        Hook_Detach(reinterpret_cast<void **>(&g_originalIl2CppApplicationQuit),
                    reinterpret_cast<void *>(&Il2CppApplicationQuitDetour));
    if (g_originalIl2CppDestroy)
        Hook_Detach(reinterpret_cast<void **>(&g_originalIl2CppDestroy),
                    reinterpret_cast<void *>(&Il2CppDestroyDetour));
    if (g_originalIl2CppDestroyImmediate)
        Hook_Detach(reinterpret_cast<void **>(&g_originalIl2CppDestroyImmediate),
                    reinterpret_cast<void *>(&Il2CppDestroyImmediateDetour));
    if (g_originalIl2CppUnityLog)
        Hook_Detach(reinterpret_cast<void **>(&g_originalIl2CppUnityLog),
                    reinterpret_cast<void *>(&Il2CppUnityLogDetour));
    if (g_originalIl2CppUnityLogWarning)
        Hook_Detach(reinterpret_cast<void **>(&g_originalIl2CppUnityLogWarning),
                    reinterpret_cast<void *>(&Il2CppUnityLogWarningDetour));
    if (g_originalIl2CppUnityLogError)
        Hook_Detach(reinterpret_cast<void **>(&g_originalIl2CppUnityLogError),
                    reinterpret_cast<void *>(&Il2CppUnityLogErrorDetour));
    if (g_originalIl2CppSceneLoaded)
        Hook_Detach(reinterpret_cast<void **>(&g_originalIl2CppSceneLoaded),
                    reinterpret_cast<void *>(&Il2CppSceneLoadedDetour));
    if (g_originalIl2CppActiveSceneChanged)
        Hook_Detach(reinterpret_cast<void **>(&g_originalIl2CppActiveSceneChanged),
                    reinterpret_cast<void *>(&Il2CppActiveSceneChangedDetour));
    if (g_originalIl2CppMouseButton)
        Hook_Detach(reinterpret_cast<void **>(&g_originalIl2CppMouseButton),
                    reinterpret_cast<void *>(&Il2CppMouseButtonDetour));
    if (g_originalIl2CppMouseButtonDown)
        Hook_Detach(reinterpret_cast<void **>(&g_originalIl2CppMouseButtonDown),
                    reinterpret_cast<void *>(&Il2CppMouseButtonDownDetour));
    if (g_originalIl2CppMouseButtonUp)
        Hook_Detach(reinterpret_cast<void **>(&g_originalIl2CppMouseButtonUp),
                    reinterpret_cast<void *>(&Il2CppMouseButtonUpDetour));

    g_backend = RuntimeEventsBackend::None;
    g_il2cpp = nullptr;
    g_capabilities = URK_RUNTIME_CAP_NONE;
    g_il2cppSceneMethods = {};
    g_il2cppCursorMethods = {};
    g_il2cppInputMethods = {};
    g_il2cppFrameMethods = {};
    g_il2cppApplicationMethods = {};
    g_il2cppObjectDestroyMethods = {};
    g_originalIl2CppSceneLoaded = nullptr;
    g_originalIl2CppActiveSceneChanged = nullptr;
    g_originalIl2CppMouseButton = nullptr;
    g_originalIl2CppMouseButtonDown = nullptr;
    g_originalIl2CppMouseButtonUp = nullptr;
    g_originalIl2CppBeforeRender = nullptr;
    g_originalIl2CppApplicationQuit = nullptr;
    g_originalIl2CppDestroy = nullptr;
    g_originalIl2CppDestroyImmediate = nullptr;
    g_originalIl2CppUnityLog = nullptr;
    g_originalIl2CppUnityLogWarning = nullptr;
    g_originalIl2CppUnityLogError = nullptr;

    g_currentScene = {};
    g_haveScene = false;
    g_sceneFailureLogged = false;
    g_cursorFailureLogged = false;
    g_inputFailureLogged = false;
    g_pumpActiveLogged = false;
    g_cursorOverrideActive = false;
    g_savedCursorState = {};
    g_menuCursorDesired.store(-1, std::memory_order_release);
    g_menuCursorLeases.Clear();
    g_menuMouseCaptureDesired.store(false, std::memory_order_release);
    g_menuMouseCaptureLeases.Clear();
    g_menuCursorLastApplyResult.store(-1, std::memory_order_release);
    g_unityMainThreadId.store(0, std::memory_order_release);
    g_mouseInputSuppressionInstalled = false;
    g_il2cppFrameHookInstallFailed = false;
    g_il2cppSceneHookInstallFailed = false;
    g_il2cppMouseHookInstallFailed = false;
    g_il2cppApplicationQuitHookInstallFailed = false;
    g_il2cppObjectDestroyHookInstallFailed = false;
    g_il2cppLogHookInstallFailed = false;
    g_il2cppFrameResolutionLogged = false;
    g_il2cppSceneResolutionLogged = false;
    g_il2cppCursorResolutionLogged = false;
    g_il2cppInputResolutionLogged = false;
    g_graphicsThreadFailureLogged.store(false, std::memory_order_release);
    g_graphicsInvokeFailureLogged.store(false, std::memory_order_release);
    g_applicationQuitObserved.store(false, std::memory_order_release);
}

bool SnapshotCursorRuntime(RuntimeEventsBackend *backend, Il2CppApi **il2cpp, uint64_t *capabilities) {
    if (!backend || !il2cpp || !capabilities)
        return false;

    std::lock_guard lock(g_eventsMutex);
    *backend = g_backend;
    *il2cpp = g_il2cpp;
    *capabilities = g_capabilities;
    return (*capabilities & URK_RUNTIME_CAP_CURSOR_CONTROL) != 0;
}

bool SnapshotRuntime(RuntimeEventsBackend *backend, Il2CppApi **il2cpp, uint64_t *capabilities) {
    if (!backend || !il2cpp || !capabilities)
        return false;

    std::lock_guard lock(g_eventsMutex);
    *backend = g_backend;
    *il2cpp = g_il2cpp;
    *capabilities = g_capabilities;
    return g_backend != RuntimeEventsBackend::None;
}

void *Il2CppMethodPointerForHook(Il2CppApi &il2cpp, const Il2CppMethod *method) {
    void *target = il2cpp.MethodPointer(method);
    if (!target) {
        const URK_Il2CppApi *publicApi = ModApi_Il2Cpp(&il2cpp);
        const char *reason = publicApi && publicApi->last_error ? publicApi->last_error() : nullptr;
        Log("[runtime][events][IL2CPP][WARNING] Managed native target resolution failed: method=%p reason=%s.", method,
            reason && reason[0] ? reason : "IL2CPP runtime did not provide a diagnostic");
    }
    return target;
}


bool InstallIl2CppFrameHook(Il2CppApi &il2cpp, const Il2CppFrameMethods &methods) {
    if (!methods.invokeOnBeforeRender) {
        Log("[runtime][events][IL2CPP][WARNING] Main-thread frame hook "
            "unavailable: Application.InvokeOnBeforeRender was not resolved.");
        return false;
    }

    void *target = Il2CppMethodPointerForHook(il2cpp, methods.invokeOnBeforeRender);
    if (!IsExecutableAddress(target)) {
        Log("[runtime][events][IL2CPP][WARNING] Refusing to hook "
            "Application.InvokeOnBeforeRender: target=%p is not executable.",
            target);
        return false;
    }

    g_originalIl2CppBeforeRender = reinterpret_cast<Il2CppBeforeRenderFn>(target);
    if (!Hook_Attach(reinterpret_cast<void **>(&g_originalIl2CppBeforeRender),
                     reinterpret_cast<void *>(&Il2CppBeforeRenderDetour))) {
        g_originalIl2CppBeforeRender = nullptr;
        Log("[runtime][events][IL2CPP][WARNING] Failed to hook "
            "Application.InvokeOnBeforeRender target=%p.",
            target);
        return false;
    }

    Log("[SUCCESS][runtime][events][IL2CPP] hooked Application.InvokeOnBeforeRender "
        "target=%p for once-per-frame main-thread dispatch.",
        target);
    return true;
}


bool InstallIl2CppLogHook(Il2CppApi &il2cpp, const Il2CppLogMethods &methods) {
    if (!methods.ready()) {
        Log("[runtime][events][IL2CPP][WARNING] Unity log capture unavailable: "
            "Debug.Log/LogWarning/LogError were not resolved.");
        return false;
    }

    const Il2CppMethod *managed[3] = {methods.log, methods.logWarning, methods.logError};
    Il2CppUnityLogFn *originals[3] = {&g_originalIl2CppUnityLog, &g_originalIl2CppUnityLogWarning,
                                      &g_originalIl2CppUnityLogError};
    Il2CppUnityLogFn detours[3] = {&Il2CppUnityLogDetour, &Il2CppUnityLogWarningDetour, &Il2CppUnityLogErrorDetour};

    for (std::size_t index = 0; index < 3; ++index) {
        void *target = Il2CppMethodPointerForHook(il2cpp, managed[index]);
        *originals[index] = reinterpret_cast<Il2CppUnityLogFn>(target);
        if (!IsExecutableAddress(target) ||
            !Hook_Attach(reinterpret_cast<void **>(originals[index]), reinterpret_cast<void *>(detours[index]))) {
            *originals[index] = nullptr;
            while (index > 0) {
                --index;
                Hook_Detach(reinterpret_cast<void **>(originals[index]), reinterpret_cast<void *>(detours[index]));
                *originals[index] = nullptr;
            }
            Log("[runtime][events][IL2CPP][WARNING] Failed to hook UnityEngine.Debug log methods; "
                "Unity output stays out of the URKit log.");
            return false;
        }
    }

    Log("[runtime][events][IL2CPP] forwarding UnityEngine.Debug output into the URKit log.");
    return true;
}


bool InstallIl2CppApplicationQuitHook(Il2CppApi &il2cpp, const Il2CppApplicationMethods &methods) {
    if (!methods.internalApplicationQuit) {
        Log("[runtime][events][IL2CPP][WARNING] Application quit hook unavailable: "
            "Application.Internal_ApplicationQuit was not resolved.");
        return false;
    }

    void *target = Il2CppMethodPointerForHook(il2cpp, methods.internalApplicationQuit);
    if (!IsExecutableAddress(target)) {
        Log("[runtime][events][IL2CPP][WARNING] Refusing to hook "
            "Application.Internal_ApplicationQuit: target=%p is not executable.",
            target);
        return false;
    }

    g_originalIl2CppApplicationQuit = reinterpret_cast<Il2CppApplicationQuitFn>(target);
    if (!Hook_Attach(reinterpret_cast<void **>(&g_originalIl2CppApplicationQuit),
                     reinterpret_cast<void *>(&Il2CppApplicationQuitDetour))) {
        g_originalIl2CppApplicationQuit = nullptr;
        Log("[runtime][events][IL2CPP][WARNING] Failed to hook "
            "Application.Internal_ApplicationQuit target=%p.",
            target);
        return false;
    }

    Log("[SUCCESS][runtime][events][IL2CPP] hooked Application.Internal_ApplicationQuit "
        "target=%p for loader shutdown.",
        target);
    return true;
}


bool InstallIl2CppObjectDestroyHooks(Il2CppApi &il2cpp, const Il2CppObjectDestroyMethods &methods) {
    if (!methods.hooks_ready()) {
        Log("[runtime][events][IL2CPP][WARNING] Object destroy request hooks unavailable: "
            "Object.Destroy(Object,Single)=%s Object.DestroyImmediate(Object,Boolean)=%s.",
            BoolText(methods.destroy != nullptr), BoolText(methods.destroyImmediate != nullptr));
        return false;
    }

    void *destroyTarget = Il2CppMethodPointerForHook(il2cpp, methods.destroy);
    void *destroyImmediateTarget = Il2CppMethodPointerForHook(il2cpp, methods.destroyImmediate);
    if (!IsExecutableAddress(destroyTarget) || !IsExecutableAddress(destroyImmediateTarget)) {
        Log("[runtime][events][IL2CPP][WARNING] Refusing object destroy request hooks: "
            "Destroy target=%p owner=%p executable=%s; "
            "DestroyImmediate target=%p owner=%p executable=%s.",
            destroyTarget, ModuleForAddress(destroyTarget), BoolText(IsExecutableAddress(destroyTarget)),
            destroyImmediateTarget, ModuleForAddress(destroyImmediateTarget),
            BoolText(IsExecutableAddress(destroyImmediateTarget)));
        return false;
    }

    g_originalIl2CppDestroy = reinterpret_cast<Il2CppDestroyFn>(destroyTarget);
    if (!Hook_Attach(reinterpret_cast<void **>(&g_originalIl2CppDestroy),
                     reinterpret_cast<void *>(&Il2CppDestroyDetour))) {
        g_originalIl2CppDestroy = nullptr;
        Log("[runtime][events][IL2CPP][WARNING] Failed to hook Object.Destroy target=%p.", destroyTarget);
        return false;
    }

    g_originalIl2CppDestroyImmediate = reinterpret_cast<Il2CppDestroyImmediateFn>(destroyImmediateTarget);
    if (!Hook_Attach(reinterpret_cast<void **>(&g_originalIl2CppDestroyImmediate),
                     reinterpret_cast<void *>(&Il2CppDestroyImmediateDetour))) {
        Hook_Detach(reinterpret_cast<void **>(&g_originalIl2CppDestroy),
                    reinterpret_cast<void *>(&Il2CppDestroyDetour));
        g_originalIl2CppDestroy = nullptr;
        g_originalIl2CppDestroyImmediate = nullptr;
        Log("[runtime][events][IL2CPP][WARNING] Failed to hook Object.DestroyImmediate target=%p; "
            "Object.Destroy hook was rolled back.",
            destroyImmediateTarget);
        return false;
    }

    Log("[SUCCESS][runtime][events][IL2CPP] object destroy request hooks active: Destroy=%p DestroyImmediate=%p.",
        destroyTarget, destroyImmediateTarget);
    return true;
}


bool InstallIl2CppMouseHooks(Il2CppApi &il2cpp, const Il2CppInputMethods &methods) {
    const Il2CppMethod *managedMethods[] = {methods.getMouseButton, methods.getMouseButtonDown,
                                            methods.getMouseButtonUp};
    void *detours[] = {reinterpret_cast<void *>(&Il2CppMouseButtonDetour),
                       reinterpret_cast<void *>(&Il2CppMouseButtonDownDetour),
                       reinterpret_cast<void *>(&Il2CppMouseButtonUpDetour)};
    Il2CppInputBoolFn *originals[] = {&g_originalIl2CppMouseButton, &g_originalIl2CppMouseButtonDown,
                                      &g_originalIl2CppMouseButtonUp};
    auto rollback = [&](int count) {
        for (int j = 0; j < count; ++j)
            Hook_Detach(reinterpret_cast<void **>(originals[j]), detours[j]);
        for (auto *original : originals)
            *original = nullptr;
    };
    for (int i = 0; i < 3; ++i) {
        void *target = Il2CppMethodPointerForHook(il2cpp, managedMethods[i]);
        if (!IsExecutableAddress(target)) {
            rollback(i);
            return false;
        }
        *originals[i] = reinterpret_cast<Il2CppInputBoolFn>(target);
        if (!Hook_Attach(reinterpret_cast<void **>(originals[i]), detours[i])) {
            rollback(i);
            return false;
        }
    }
    return true;
}


bool InstallIl2CppSceneHooks(Il2CppApi &il2cpp, const Il2CppSceneMethods &methods) {
    bool installed = false;
    const auto install = [&](const Il2CppMethod *method, auto *&original, void *detour, const char *name) {
        if (!method)
            return;
        void *target = Il2CppMethodPointerForHook(il2cpp, method);
        if (!IsExecutableAddress(target)) {
            Log("[runtime][events][IL2CPP][WARNING] Refusing to hook %s: target=%p is not executable.", name, target);
            return;
        }
        original = FunctionPointerFromAddress<std::remove_reference_t<decltype(original)>>(target);
        if (Hook_Attach(reinterpret_cast<void **>(&original), detour)) {
            installed = true;
            Log("[SUCCESS][runtime][events][IL2CPP] hooked %s target=%p.", name, target);
        } else {
            original = nullptr;
            Log("[runtime][events][IL2CPP][WARNING] Failed to hook %s.", name);
        }
    };
    install(methods.internalSceneLoaded, g_originalIl2CppSceneLoaded,
            reinterpret_cast<void *>(&Il2CppSceneLoadedDetour), "SceneManager.Internal_SceneLoaded");
    install(methods.internalActiveSceneChanged, g_originalIl2CppActiveSceneChanged,
            reinterpret_cast<void *>(&Il2CppActiveSceneChangedDetour), "SceneManager.Internal_ActiveSceneChanged");
    return installed;
}


bool Il2CppActivationSettledLocked() {
    const bool frameSettled = (g_capabilities & URK_RUNTIME_CAP_MAIN_THREAD) != 0 || g_il2cppFrameHookInstallFailed;
    const bool graphicsSettled =
        (g_capabilities & URK_RUNTIME_CAP_GRAPHICS_DEVICE_TYPE) != 0 || g_il2cppFrameHookInstallFailed;
    const bool sceneSettled = (g_capabilities & URK_RUNTIME_CAP_SCENE_EVENTS) != 0 || g_il2cppSceneHookInstallFailed;
    const bool cursorSettled = (g_capabilities & URK_RUNTIME_CAP_CURSOR_CONTROL) != 0;
    const bool inputSettled = (g_capabilities & URK_RUNTIME_CAP_INPUT) != 0;
    const bool mouseSettled = g_mouseInputSuppressionInstalled || g_il2cppMouseHookInstallFailed;
    return frameSettled && graphicsSettled && sceneSettled && cursorSettled && inputSettled && mouseSettled;
}


uint64_t TryActivateIl2CppRuntimeEvents(Il2CppApi &il2cpp) {
    uint64_t capabilities = URK_RUNTIME_CAP_NONE;
    bool needFrame = false;
    bool needGraphics = false;
    bool needScene = false;
    bool needCursor = false;
    bool needInput = false;
    bool needMouseSuppression = false;
    bool needApplicationQuit = false;
    bool needLogs = false;

    {
        std::lock_guard lock(g_eventsMutex);
        if (g_backend != RuntimeEventsBackend::Il2Cpp || g_il2cpp != &il2cpp)
            return g_capabilities;
        capabilities = g_capabilities;
        needFrame = (capabilities & URK_RUNTIME_CAP_MAIN_THREAD) == 0 && !g_il2cppFrameHookInstallFailed;
        needGraphics = (capabilities & URK_RUNTIME_CAP_GRAPHICS_DEVICE_TYPE) == 0 && !g_il2cppFrameHookInstallFailed;
        needScene = (capabilities & URK_RUNTIME_CAP_SCENE_EVENTS) == 0 && !g_il2cppSceneHookInstallFailed;
        needCursor = (capabilities & URK_RUNTIME_CAP_CURSOR_CONTROL) == 0;
        needInput = (capabilities & URK_RUNTIME_CAP_INPUT) == 0;
        needMouseSuppression = !g_mouseInputSuppressionInstalled && !g_il2cppMouseHookInstallFailed;
        needApplicationQuit = g_originalIl2CppApplicationQuit == nullptr && !g_il2cppApplicationQuitHookInstallFailed;
        needLogs = Config_Current().unityLogs && g_originalIl2CppUnityLog == nullptr && !g_il2cppLogHookInstallFailed;
        if (!needFrame && !needGraphics && !needScene && !needCursor && !needInput && !needMouseSuppression &&
            !needApplicationQuit && !needLogs) {
            return capabilities;
        }
    }

    if (needLogs) {
        const Il2CppLogMethods logMethods = ResolveIl2CppLogMethods(il2cpp);
        {
            std::lock_guard lock(g_eventsMutex);
            if (g_backend != RuntimeEventsBackend::Il2Cpp || g_il2cpp != &il2cpp)
                return g_capabilities;
            g_il2cppLogMethods = logMethods;
        }
        if (logMethods.ready() && !InstallIl2CppLogHook(il2cpp, logMethods)) {
            std::lock_guard lock(g_eventsMutex);
            g_il2cppLogHookInstallFailed = true;
        }
    }

    if (needFrame || needGraphics) {
        const Il2CppFrameMethods frameMethods = ResolveIl2CppFrameMethods(il2cpp);
        bool logIncompleteFrameMethods = false;
        {
            std::lock_guard lock(g_eventsMutex);
            if (g_backend != RuntimeEventsBackend::Il2Cpp || g_il2cpp != &il2cpp)
                return g_capabilities;
            g_il2cppFrameMethods = frameMethods;
            if (!frameMethods.ready() && !g_il2cppFrameResolutionLogged) {
                g_il2cppFrameResolutionLogged = true;
                logIncompleteFrameMethods = true;
            }
        }
        if (logIncompleteFrameMethods) {
            LogFrameMethodsUnavailable(
                "IL2CPP", "Main-thread frame pump or graphics query not ready yet; late activation will retry",
                frameMethods);
        }

        const bool frameInstalled =
            needFrame && frameMethods.frame_ready() && InstallIl2CppFrameHook(il2cpp, frameMethods);
        bool graphicsActivated = false;
        {
            std::lock_guard lock(g_eventsMutex);
            if (frameInstalled) {
                g_capabilities |= URK_RUNTIME_CAP_MAIN_THREAD;
            } else if (needFrame && frameMethods.frame_ready()) {
                g_il2cppFrameHookInstallFailed = true;
            }
            if ((g_capabilities & URK_RUNTIME_CAP_MAIN_THREAD) != 0 && frameMethods.graphics_ready() &&
                (g_capabilities & URK_RUNTIME_CAP_GRAPHICS_DEVICE_TYPE) == 0) {
                g_capabilities |= URK_RUNTIME_CAP_GRAPHICS_DEVICE_TYPE;
                graphicsActivated = true;
            }
            capabilities = g_capabilities;
        }
        if (frameInstalled) {
            MainThread_SetDispatchTargetAvailable(true);
            Log("[SUCCESS][runtime][events][IL2CPP] main-thread frame pump activated after "
                "Application.InvokeOnBeforeRender became available.");
        }
        if (graphicsActivated) {
            Log("[runtime][events][IL2CPP] SystemInfo.graphicsDeviceType helper "
                "activated after UnityEngine methods became available.");
        }
    }

    if (needScene) {
        const Il2CppSceneMethods sceneMethods = ResolveIl2CppSceneMethods(il2cpp);
        bool logIncompleteSceneMethods = false;
        {
            std::lock_guard lock(g_eventsMutex);
            if (!sceneMethods.ready() && !g_il2cppSceneResolutionLogged) {
                g_il2cppSceneResolutionLogged = true;
                logIncompleteSceneMethods = true;
            }
        }
        if (logIncompleteSceneMethods) {
            LogSceneMethodsUnavailable("IL2CPP", "Scene queries not ready yet; late activation will retry",
                                       sceneMethods);
        }
        if (sceneMethods.ready()) {
            {
                std::lock_guard lock(g_eventsMutex);
                if (g_backend != RuntimeEventsBackend::Il2Cpp || g_il2cpp != &il2cpp)
                    return g_capabilities;
                g_il2cppSceneMethods = sceneMethods;
            }

            const bool sceneInstalled = InstallIl2CppSceneHooks(il2cpp, sceneMethods);
            {
                std::lock_guard lock(g_eventsMutex);
                g_capabilities |= URK_RUNTIME_CAP_SCENE_EVENTS;
                g_il2cppSceneHookInstallFailed = !sceneInstalled;
                capabilities = g_capabilities;
            }
            if (sceneInstalled) {
                Log("[SUCCESS][runtime][events][IL2CPP] scene hooks activated after UnityEngine methods became "
                    "available.");
            } else {
                Log("[runtime][events][IL2CPP][WARNING] Native scene hooks are unavailable; "
                    "active-scene polling fallback is enabled. Additive scene loads that do not change the active "
                    "scene cannot be observed by the fallback.");
            }
        }
    }

    if (needApplicationQuit) {
        const Il2CppApplicationMethods applicationMethods = ResolveIl2CppApplicationMethods(il2cpp);
        {
            std::lock_guard lock(g_eventsMutex);
            if (g_backend != RuntimeEventsBackend::Il2Cpp || g_il2cpp != &il2cpp)
                return g_capabilities;
            g_il2cppApplicationMethods = applicationMethods;
        }

        const bool applicationInstalled =
            applicationMethods.ready() && InstallIl2CppApplicationQuitHook(il2cpp, applicationMethods);
        {
            std::lock_guard lock(g_eventsMutex);
            if (!applicationInstalled && applicationMethods.ready())
                g_il2cppApplicationQuitHookInstallFailed = true;
        }
        if (applicationInstalled) {
            Log("[SUCCESS][runtime][events][IL2CPP] Application quit hook activated after "
                "Application.Internal_ApplicationQuit became available.");
        }
    }

    if (needCursor) {
        const Il2CppCursorMethods cursorMethods = ResolveIl2CppCursorMethods(il2cpp);
        bool logIncompleteCursorMethods = false;
        {
            std::lock_guard lock(g_eventsMutex);
            if (!cursorMethods.ready() && !g_il2cppCursorResolutionLogged) {
                g_il2cppCursorResolutionLogged = true;
                logIncompleteCursorMethods = true;
            }
        }
        if (logIncompleteCursorMethods) {
            LogCursorMethodsUnavailable("IL2CPP", "Cursor control not ready yet; late activation will retry",
                                        cursorMethods);
        }
        if (cursorMethods.ready()) {
            std::lock_guard lock(g_eventsMutex);
            if (g_backend != RuntimeEventsBackend::Il2Cpp || g_il2cpp != &il2cpp)
                return g_capabilities;
            g_il2cppCursorMethods = cursorMethods;
            g_capabilities |= URK_RUNTIME_CAP_CURSOR_CONTROL;
            capabilities = g_capabilities;
            Log("[SUCCESS][runtime][events][IL2CPP] cursor control activated after "
                "UnityEngine methods became available.");
        }
    }

    if (needInput || needMouseSuppression) {
        const Il2CppInputMethods inputMethods = ResolveIl2CppInputMethods(il2cpp);
        bool logIncompleteInputMethods = false;
        {
            std::lock_guard lock(g_eventsMutex);
            if (!inputMethods.ready() && !g_il2cppInputResolutionLogged) {
                g_il2cppInputResolutionLogged = true;
                logIncompleteInputMethods = true;
            }
        }
        if (logIncompleteInputMethods) {
            LogInputMethodsUnavailable(
                "IL2CPP", "Legacy input helpers not ready yet; searched InputLegacyModule, CoreModule, and UnityEngine",
                inputMethods);
        }
        if (inputMethods.readyForMouseSuppression() || inputMethods.ready()) {
            std::lock_guard lock(g_eventsMutex);
            if (g_backend != RuntimeEventsBackend::Il2Cpp || g_il2cpp != &il2cpp)
                return g_capabilities;
            g_il2cppInputMethods = inputMethods;
            if (needInput && inputMethods.ready())
                g_capabilities |= URK_RUNTIME_CAP_INPUT;
            capabilities = g_capabilities;
        }
        const bool suppressionInstalled = inputMethods.readyForMouseSuppression() && needMouseSuppression &&
                                          InstallIl2CppMouseHooks(il2cpp, inputMethods);
        if (suppressionInstalled) {
            std::lock_guard lock(g_eventsMutex);
            g_mouseInputSuppressionInstalled = true;
            Log("[SUCCESS][runtime][events][IL2CPP] mouse input suppression activated after "
                "UnityEngine methods became available.");
        } else if (inputMethods.readyForMouseSuppression() && needMouseSuppression) {
            std::lock_guard lock(g_eventsMutex);
            g_il2cppMouseHookInstallFailed = true;
            Log("[runtime][events][IL2CPP][WARNING] Mouse input suppression hooks "
                "could not be installed; the same targets will not be retried.");
        }
    }

    return capabilities;
}


struct Il2CppActivationWorkerArgs {
    Il2CppApi *il2cpp = nullptr;
    uint32_t generation = 0;
};

bool RuntimeEventsGenerationMatches(uint32_t generation) {
    return g_eventsGeneration.load(std::memory_order_acquire) == generation;
}

void LogLateActivationResult(const char *backend, uint64_t capabilities, bool mouseSuppressionReady) {
    const bool mainThreadReady = (capabilities & URK_RUNTIME_CAP_MAIN_THREAD) != 0;
    const bool graphicsReady = (capabilities & URK_RUNTIME_CAP_GRAPHICS_DEVICE_TYPE) != 0;
    const bool sceneReady = (capabilities & URK_RUNTIME_CAP_SCENE_EVENTS) != 0;
    const bool cursorReady = (capabilities & URK_RUNTIME_CAP_CURSOR_CONTROL) != 0;
    const bool inputReady = (capabilities & URK_RUNTIME_CAP_INPUT) != 0;
    if (mainThreadReady && graphicsReady && sceneReady && cursorReady && inputReady && mouseSuppressionReady)
        return;

    const char *format = mainThreadReady && graphicsReady
                             ? "[runtime][events][%s][INFO] Late UnityEngine capability probing completed; "
                               "core render services are ready and unavailable optional services will use native "
                               "fallbacks where supported: main thread=%s graphics device=%s scene events=%s "
                               "cursor control=%s legacy input=%s mouse suppression=%s."
                             : "[runtime][events][%s][WARNING] Late UnityEngine capability probing stopped with "
                               "required render services unavailable: main thread=%s graphics device=%s "
                               "scene events=%s cursor control=%s legacy input=%s mouse suppression=%s.";
    Log(format, backend ? backend : "unknown", BoolText(mainThreadReady), BoolText(graphicsReady), BoolText(sceneReady),
        BoolText(cursorReady), BoolText(inputReady), BoolText(mouseSuppressionReady));
}




DWORD WINAPI Il2CppActivationWorkerProc(void *user) {
    auto *args = static_cast<Il2CppActivationWorkerArgs *>(user);
    Il2CppApi *il2cpp = args ? args->il2cpp : nullptr;
    const uint32_t generation = args ? args->generation : 0;
    delete args;

    constexpr DWORD kInitialDelayMs = 250;
    constexpr DWORD kRetryDelayMs = 500;
    constexpr int kMaxAttempts = 120;

    for (int attempt = 0; il2cpp && attempt < kMaxAttempts; ++attempt) {
        if (!RuntimeEventsGenerationMatches(generation) || ModLifecycle_ShutdownStarted()) {
            break;
        }

        Sleep(attempt == 0 ? kInitialDelayMs : kRetryDelayMs);

        if (!RuntimeEventsGenerationMatches(generation) || ModLifecycle_ShutdownStarted()) {
            break;
        }

        Il2CppRuntimeThreadScope scope(*il2cpp);
        if (!scope.IsAttached())
            continue;

        TryActivateIl2CppRuntimeEvents(*il2cpp);
        {
            std::lock_guard lock(g_eventsMutex);
            if (Il2CppActivationSettledLocked())
                break;
        }
    }

    if (il2cpp && RuntimeEventsGenerationMatches(generation) && !ModLifecycle_ShutdownStarted()) {
        uint64_t capabilities = URK_RUNTIME_CAP_NONE;
        bool mouseSuppressionReady = false;
        {
            std::lock_guard lock(g_eventsMutex);
            capabilities = g_capabilities;
            mouseSuppressionReady = g_mouseInputSuppressionInstalled;
        }
        LogLateActivationResult("IL2CPP", capabilities, mouseSuppressionReady);
    }

    {
        std::lock_guard lock(g_il2cppActivationWorkerMutex);
        if (g_il2cppActivationWorkerId == GetCurrentThreadId()) {
            g_il2cppActivationWorkerId = 0;
        }
    }

    return 0;
}

void StartIl2CppActivationWorker(Il2CppApi &il2cpp) {
    std::lock_guard lock(g_il2cppActivationWorkerMutex);
    if (g_il2cppActivationWorker) {
        if (WaitForSingleObject(g_il2cppActivationWorker, 0) == WAIT_OBJECT_0) {
            CloseHandle(g_il2cppActivationWorker);
            g_il2cppActivationWorker = nullptr;
            g_il2cppActivationWorkerId = 0;
        } else {
            return;
        }
    }

    const uint32_t generation = g_eventsGeneration.load(std::memory_order_acquire);
    auto *args = new (std::nothrow) Il2CppActivationWorkerArgs{&il2cpp, generation};
    if (!args) {
        Log("[runtime][events][IL2CPP][WARNING] Late UnityEngine event "
            "activation worker could not allocate its startup context.");
        return;
    }
    DWORD threadId = 0;
    HANDLE thread = CreateThread(nullptr, 0, &Il2CppActivationWorkerProc, args, CREATE_SUSPENDED, &threadId);
    if (!thread) {
        delete args;
        Log("[runtime][events][IL2CPP][WARNING] Late UnityEngine event "
            "activation worker could not be created: error=%lu.",
            GetLastError());
        return;
    }

    g_il2cppActivationWorker = thread;
    g_il2cppActivationWorkerId = threadId;
    ResumeThread(thread);
    Log("[runtime][events][IL2CPP] Late UnityEngine event activation worker "
        "started.");
}

void StopIl2CppActivationWorker() {
    HANDLE thread = nullptr;
    DWORD threadId = 0;
    {
        std::lock_guard lock(g_il2cppActivationWorkerMutex);
        g_eventsGeneration.fetch_add(1, std::memory_order_acq_rel);
        thread = g_il2cppActivationWorker;
        threadId = g_il2cppActivationWorkerId;
    }

    if (thread && threadId != GetCurrentThreadId()) {
        const DWORD waitResult = WaitForSingleObject(thread, 2000);
        if (waitResult == WAIT_OBJECT_0) {
            std::lock_guard lock(g_il2cppActivationWorkerMutex);
            if (g_il2cppActivationWorker == thread) {
                CloseHandle(thread);
                g_il2cppActivationWorker = nullptr;
                g_il2cppActivationWorkerId = 0;
            }
        } else {
            Log("[runtime][events][IL2CPP][WARNING] Activation worker did not stop within 2000 ms; "
                "the loader remains pinned and the worker handle is retained.");
        }
    }
}

void PumpCursorControl() {
    std::lock_guard lock(g_eventsMutex);

    const int desired = g_menuCursorDesired.load(std::memory_order_acquire);
    if (desired < 0)
        return;


    if (desired != 0) {
        if (g_cursorOverrideActive) {
            if (!SetCursorLockState(URK_CURSOR_LOCK_NONE) || !SetCursorVisible(true)) {
                if (!g_cursorFailureLogged) {
                    Log("[runtime][events][%s][WARNING] Cursor menu "
                        "override reapply failed.",
                        BackendText(g_backend));
                    g_cursorFailureLogged = true;
                }
                g_cursorOverrideActive = false;
                g_menuCursorLastApplyResult.store(0, std::memory_order_release);
            } else {
                g_menuCursorLastApplyResult.store(1, std::memory_order_release);
            }
            return;
        }

        CursorState current{};
        if (!ReadCursorState(&current) || !SetCursorLockState(URK_CURSOR_LOCK_NONE) || !SetCursorVisible(true)) {
            if (!g_cursorFailureLogged) {
                Log("[runtime][events][%s][WARNING] Cursor menu override failed; "
                    "visibility/lock could not be applied.",
                    BackendText(g_backend));
                g_cursorFailureLogged = true;
            }

            g_cursorOverrideActive = false;
            g_menuCursorLastApplyResult.store(0, std::memory_order_release);
            return;
        }

        g_savedCursorState = current;
        g_cursorOverrideActive = true;
        g_menuCursorLastApplyResult.store(1, std::memory_order_release);
        Log("[runtime][events][%s] cursor override enabled for native menu "
            "(saved visible=%s lockState=%d).",
            BackendText(g_backend), BoolText(current.visible), current.lockState);
        return;
    }

    if (!g_cursorOverrideActive) {
        g_menuCursorLastApplyResult.store(1, std::memory_order_release);
        return;
    }

    if (!ApplyCursorState(g_savedCursorState)) {
        if (!g_cursorFailureLogged) {
            Log("[runtime][events][%s][WARNING] Cursor restore failed; "
                "saved visibility/lock could not be restored.",
                BackendText(g_backend));
            g_cursorFailureLogged = true;
        }

        g_cursorOverrideActive = false;
        g_menuCursorLastApplyResult.store(0, std::memory_order_release);
        return;
    }

    Log("[runtime][events][%s] cursor override disabled; restored visible=%s "
        "lockState=%d.",
        BackendText(g_backend), BoolText(g_savedCursorState.visible), g_savedCursorState.lockState);
    g_cursorOverrideActive = false;
    g_menuCursorLastApplyResult.store(1, std::memory_order_release);
}
} // namespace

void RuntimeEvents_Reset() {
    StopIl2CppActivationWorker();
    MainThread_SetDispatchTargetAvailable(false);
    std::lock_guard lock(g_eventsMutex);
    ResetStateLocked();
}

void RuntimeEvents_StopWorkers() {
    StopIl2CppActivationWorker();
}

uint64_t RuntimeEvents_Capabilities() {
    std::lock_guard lock(g_eventsMutex);
    return g_capabilities;
}


void RuntimeEvents_AfterModsLoaded() {
    RuntimeEventsBackend backend = RuntimeEventsBackend::None;
    Il2CppApi *il2cpp = nullptr;
    uint64_t capabilities = URK_RUNTIME_CAP_NONE;
    bool mouseSuppressionInstalled = false;
    {
        std::lock_guard lock(g_eventsMutex);
        backend = g_backend;
        il2cpp = g_il2cpp;
        capabilities = g_capabilities;
        mouseSuppressionInstalled = g_mouseInputSuppressionInstalled;
    }

    if (backend == RuntimeEventsBackend::None)
        return;

    const uint64_t required = URK_RUNTIME_CAP_MAIN_THREAD | URK_RUNTIME_CAP_GRAPHICS_DEVICE_TYPE |
                              URK_RUNTIME_CAP_SCENE_EVENTS | URK_RUNTIME_CAP_CURSOR_CONTROL | URK_RUNTIME_CAP_INPUT;
    if ((capabilities & required) == required && mouseSuppressionInstalled)
        return;

    bool settled = false;
    bool frameHookFailed = false;
    bool sceneHookFailed = false;
    bool mouseHookFailed = false;
    {
        std::lock_guard lock(g_eventsMutex);
        if (backend == RuntimeEventsBackend::Il2Cpp) {
            settled = Il2CppActivationSettledLocked();
            frameHookFailed = g_il2cppFrameHookInstallFailed;
            sceneHookFailed = g_il2cppSceneHookInstallFailed;
            mouseHookFailed = g_il2cppMouseHookInstallFailed;
        }
    }
    if (settled) {
        Log("[runtime][events][%s][WARNING] Late UnityEngine event activation "
            "not started: unresolved services have deterministic hook failures "
            "(frame=%s scene=%s mouse suppression=%s); the same targets will not "
            "be retried.",
            BackendText(backend), BoolText(frameHookFailed), BoolText(sceneHookFailed), BoolText(mouseHookFailed));
        return;
    }

    if (backend == RuntimeEventsBackend::Il2Cpp && il2cpp) {
        StartIl2CppActivationWorker(*il2cpp);
    }
}


uint64_t RuntimeEvents_ConfigureIl2Cpp(Il2CppApi &il2cpp) {
    RuntimeEvents_Reset();

    Il2CppRuntimeThreadScope scope(il2cpp);
    if (!scope.IsAttached()) {
        Log("[runtime][events][IL2CPP][WARNING] Object destroy request hook configuration skipped: "
            "failed to attach current thread to the IL2CPP domain.");
        return URK_RUNTIME_CAP_NONE;
    }

    const Il2CppObjectDestroyMethods objectDestroyMethods = ResolveIl2CppObjectDestroyMethods(il2cpp);
    {
        std::lock_guard lock(g_eventsMutex);
        g_backend = RuntimeEventsBackend::Il2Cpp;
        g_il2cpp = &il2cpp;
        g_il2cppObjectDestroyMethods = objectDestroyMethods;
    }
    const bool objectDestroyInstalled = InstallIl2CppObjectDestroyHooks(il2cpp, objectDestroyMethods);
    uint64_t capabilities = URK_RUNTIME_CAP_NONE;
    if (objectDestroyInstalled)
        capabilities |= URK_RUNTIME_CAP_OBJECT_DESTROY_REQUEST_EVENTS;

    {
        std::lock_guard lock(g_eventsMutex);
        g_capabilities = capabilities;
        g_il2cppObjectDestroyHookInstallFailed = !objectDestroyInstalled;
    }

    capabilities = TryActivateIl2CppRuntimeEvents(il2cpp);
    Log("[runtime][events][IL2CPP] main thread=%s graphics device=%s scene events=%s "
        "cursor control=%s input=%s object destroy requests=%s.",
        BoolText((capabilities & URK_RUNTIME_CAP_MAIN_THREAD) != 0),
        BoolText((capabilities & URK_RUNTIME_CAP_GRAPHICS_DEVICE_TYPE) != 0),
        BoolText((capabilities & URK_RUNTIME_CAP_SCENE_EVENTS) != 0),
        BoolText((capabilities & URK_RUNTIME_CAP_CURSOR_CONTROL) != 0),
        BoolText((capabilities & URK_RUNTIME_CAP_INPUT) != 0), BoolText(objectDestroyInstalled));
    return capabilities;
}

void RuntimeEvents_Pump() {
    if (ModLifecycle_ShutdownStarted())
        return;
    if (g_runtimePumpActive)
        return;

    g_runtimePumpActive = true;
    struct PumpScope {
        ~PumpScope() {
            g_runtimePumpActive = false;
        }
    } pumpScope;

    RuntimeEventsBackend backend = RuntimeEventsBackend::None;
    Il2CppApi *il2cpp = nullptr;
    uint64_t capabilities = URK_RUNTIME_CAP_NONE;

    {
        std::lock_guard lock(g_eventsMutex);
        backend = g_backend;
        il2cpp = g_il2cpp;
        capabilities = g_capabilities;
    }

    auto pump = [&]() {
        if (capabilities == URK_RUNTIME_CAP_NONE)
            return;

        {
            std::lock_guard lock(g_eventsMutex);
            if (!g_pumpActiveLogged) {
                Log("[runtime][events][%s] pump active "
                    "(capabilities=0x%llX).",
                    BackendText(backend), static_cast<unsigned long long>(capabilities));
                g_pumpActiveLogged = true;
            }
        }

        if ((capabilities & URK_RUNTIME_CAP_SCENE_EVENTS) != 0)
            PumpSceneEvents();

        if ((capabilities & URK_RUNTIME_CAP_CURSOR_CONTROL) != 0)
            PumpCursorControl();

        MainThread_Drain();
    };

    if (backend == RuntimeEventsBackend::Il2Cpp) {
        if (!il2cpp)
            return;
        if (capabilities == URK_RUNTIME_CAP_NONE)
            return;

        Il2CppRuntimeThreadScope scope(*il2cpp);
        if (!scope.IsAttached())
            return;

        pump();
        return;
    }
}

void RuntimeEvents_ObserveScene(const URK_SceneInfo &scene, bool distinct) {
    if (ModLifecycle_ShutdownStarted())
        return;
    SceneTransition transition{};
    {
        std::lock_guard lock(g_eventsMutex);
        if (!g_haveScene) {
            g_currentScene = scene;
            g_haveScene = true;
            transition.firstScene = true;
        } else if (distinct || !SameScene(g_currentScene, scene)) {
            transition.previous = g_currentScene;
            g_currentScene = scene;
            transition.changed = true;
        }
    }
    DispatchTransition(scene, transition);
}

void RuntimeEvents_SetMainThread(DWORD threadId) {
    g_unityMainThreadId.store(threadId, std::memory_order_release);
}


namespace {





} // namespace


int RuntimeEvents_CurrentScene(URK_SceneInfo *scene) {
    if (!scene || scene->size < sizeof(URK_SceneInfo))
        return 0;

    std::lock_guard lock(g_eventsMutex);
    if (!g_haveScene)
        return 0;
    *scene = g_currentScene;
    scene->size = sizeof(URK_SceneInfo);
    return 1;
}

int RuntimeEvents_MenuCursorSetOpen(void *ownerModule, int open) {
    // Counted per module so one mod can't release another's lease.
    if (!ownerModule)
        return 0;
    {
        std::lock_guard lock(g_eventsMutex);
        if ((g_capabilities & URK_RUNTIME_CAP_CURSOR_CONTROL) == 0)
            return 0;

        if (open) {
            g_menuCursorLeases.Acquire(ownerModule);
        } else {
            g_menuCursorLeases.Release(ownerModule);
        }
        g_menuCursorDesired.store(g_menuCursorLeases.AnyOpen() ? 1 : 0, std::memory_order_release);
    }
    // Often called from the render thread; the event pump applies it on the Unity thread.
    if (g_unityMainThreadId.load(std::memory_order_acquire) == GetCurrentThreadId())
        RuntimeEvents_Pump();
    g_menuCursorLastApplyResult.store(1, std::memory_order_release);
    return 1;
}

int RuntimeEvents_MenuMouseCaptureSet(void *ownerModule, int capture) {
    if (!ownerModule)
        return 0;
    std::lock_guard lock(g_eventsMutex);
    if (!g_menuMouseCaptureLeases.Set(ownerModule, capture != 0))
        return 0;
    g_menuMouseCaptureDesired.store(g_menuMouseCaptureLeases.AnyOpen(), std::memory_order_release);
    return 1;
}

int RuntimeEvents_UnregisterModule(void *module) {
    if (!module)
        return 0;
    std::size_t released = 0;
    std::size_t releasedMouseCapture = 0;
    {
        std::lock_guard lock(g_eventsMutex);
        released = g_menuCursorLeases.ReleaseOwner(module);
        releasedMouseCapture = g_menuMouseCaptureLeases.ReleaseOwner(module);
        if (released != 0)
            g_menuCursorDesired.store(g_menuCursorLeases.AnyOpen() ? 1 : 0, std::memory_order_release);
        if (releasedMouseCapture != 0)
            g_menuMouseCaptureDesired.store(g_menuMouseCaptureLeases.AnyOpen(), std::memory_order_release);
    }
    if (released != 0) {
        Log("[runtime][events] released %zu cursor lease(s) for unloading module=%p.", released, module);
        if (g_unityMainThreadId.load(std::memory_order_acquire) == GetCurrentThreadId())
            RuntimeEvents_Pump();
    }
    return static_cast<int>(released + releasedMouseCapture);
}

int RuntimeEvents_CursorStateGet(URK_CursorState *state) {
    if (!state || state->size < sizeof(URK_CursorState))
        return 0;

    RuntimeEventsBackend backend = RuntimeEventsBackend::None;
    Il2CppApi *il2cpp = nullptr;
    uint64_t capabilities = URK_RUNTIME_CAP_NONE;
    if (!SnapshotCursorRuntime(&backend, &il2cpp, &capabilities))
        return 0;

    CursorState current{};
    switch (backend) {
        case RuntimeEventsBackend::Il2Cpp: {
            if (!il2cpp)
                return 0;
            Il2CppRuntimeThreadScope scope(*il2cpp);
            if (!scope.IsAttached() || !ReadCursorState(&current))
                return 0;
            break;
        }
        default:
            return 0;
    }

    state->size = sizeof(URK_CursorState);
    state->visible = current.visible ? 1 : 0;
    state->lockState = current.lockState;
    return 1;
}

int RuntimeEvents_CursorStateSet(const URK_CursorState *state) {
    if (!state || state->size < sizeof(URK_CursorState))
        return 0;

    RuntimeEventsBackend backend = RuntimeEventsBackend::None;
    Il2CppApi *il2cpp = nullptr;
    uint64_t capabilities = URK_RUNTIME_CAP_NONE;
    if (!SnapshotCursorRuntime(&backend, &il2cpp, &capabilities))
        return 0;

    CursorState desired{};
    desired.visible = state->visible != 0;
    desired.lockState = state->lockState;
    switch (backend) {
        case RuntimeEventsBackend::Il2Cpp: {
            if (!il2cpp)
                return 0;
            Il2CppRuntimeThreadScope scope(*il2cpp);
            return scope.IsAttached() && ApplyCursorState(desired) ? 1 : 0;
        }
        default:
            return 0;
    }
}

int RuntimeEvents_InputGetKey(int32_t keyCode) {
    RuntimeEventsBackend backend = RuntimeEventsBackend::None;
    Il2CppApi *il2cpp = nullptr;
    uint64_t capabilities = URK_RUNTIME_CAP_NONE;
    if (!SnapshotRuntime(&backend, &il2cpp, &capabilities) || (capabilities & URK_RUNTIME_CAP_INPUT) == 0) {
        return 0;
    }

    switch (backend) {
        case RuntimeEventsBackend::Il2Cpp: {
            if (!il2cpp)
                return 0;
            Il2CppRuntimeThreadScope scope(*il2cpp);
            return scope.IsAttached() && ReadInputStateByKind(keyCode, 0) ? 1 : 0;
        }
        default:
            return 0;
    }
}

int RuntimeEvents_InputGetKeyDown(int32_t keyCode) {
    RuntimeEventsBackend backend = RuntimeEventsBackend::None;
    Il2CppApi *il2cpp = nullptr;
    uint64_t capabilities = URK_RUNTIME_CAP_NONE;
    if (!SnapshotRuntime(&backend, &il2cpp, &capabilities) || (capabilities & URK_RUNTIME_CAP_INPUT) == 0) {
        return 0;
    }

    switch (backend) {
        case RuntimeEventsBackend::Il2Cpp: {
            if (!il2cpp)
                return 0;
            Il2CppRuntimeThreadScope scope(*il2cpp);
            return scope.IsAttached() && ReadInputStateByKind(keyCode, 1) ? 1 : 0;
        }
        default:
            return 0;
    }
}

int RuntimeEvents_InputGetKeyUp(int32_t keyCode) {
    RuntimeEventsBackend backend = RuntimeEventsBackend::None;
    Il2CppApi *il2cpp = nullptr;
    uint64_t capabilities = URK_RUNTIME_CAP_NONE;
    if (!SnapshotRuntime(&backend, &il2cpp, &capabilities) || (capabilities & URK_RUNTIME_CAP_INPUT) == 0) {
        return 0;
    }

    switch (backend) {
        case RuntimeEventsBackend::Il2Cpp: {
            if (!il2cpp)
                return 0;
            Il2CppRuntimeThreadScope scope(*il2cpp);
            return scope.IsAttached() && ReadInputStateByKind(keyCode, 2) ? 1 : 0;
        }
        default:
            return 0;
    }
}

int RuntimeEvents_InputGetMouseButton(int32_t button) {
    RuntimeEventsBackend backend = RuntimeEventsBackend::None;
    Il2CppApi *il2cpp = nullptr;
    uint64_t capabilities = URK_RUNTIME_CAP_NONE;
    if (!SnapshotRuntime(&backend, &il2cpp, &capabilities) || (capabilities & URK_RUNTIME_CAP_INPUT) == 0) {
        return 0;
    }

    switch (backend) {
        case RuntimeEventsBackend::Il2Cpp: {
            if (!il2cpp)
                return 0;
            Il2CppRuntimeThreadScope scope(*il2cpp);
            return scope.IsAttached() && ReadInputStateByKind(button, 3) ? 1 : 0;
        }
        default:
            return 0;
    }
}

int RuntimeEvents_InputGetMouseButtonDown(int32_t button) {
    RuntimeEventsBackend backend = RuntimeEventsBackend::None;
    Il2CppApi *il2cpp = nullptr;
    uint64_t capabilities = URK_RUNTIME_CAP_NONE;
    if (!SnapshotRuntime(&backend, &il2cpp, &capabilities) || (capabilities & URK_RUNTIME_CAP_INPUT) == 0) {
        return 0;
    }

    switch (backend) {
        case RuntimeEventsBackend::Il2Cpp: {
            if (!il2cpp)
                return 0;
            Il2CppRuntimeThreadScope scope(*il2cpp);
            return scope.IsAttached() && ReadInputStateByKind(button, 4) ? 1 : 0;
        }
        default:
            return 0;
    }
}

int RuntimeEvents_InputGetMouseButtonUp(int32_t button) {
    RuntimeEventsBackend backend = RuntimeEventsBackend::None;
    Il2CppApi *il2cpp = nullptr;
    uint64_t capabilities = URK_RUNTIME_CAP_NONE;
    if (!SnapshotRuntime(&backend, &il2cpp, &capabilities) || (capabilities & URK_RUNTIME_CAP_INPUT) == 0) {
        return 0;
    }

    switch (backend) {
        case RuntimeEventsBackend::Il2Cpp: {
            if (!il2cpp)
                return 0;
            Il2CppRuntimeThreadScope scope(*il2cpp);
            return scope.IsAttached() && ReadInputStateByKind(button, 5) ? 1 : 0;
        }
        default:
            return 0;
    }
}

int32_t RuntimeEvents_GraphicsDeviceType() {
    RuntimeEventsBackend backend = RuntimeEventsBackend::None;
    Il2CppApi *il2cpp = nullptr;
    uint64_t capabilities = URK_RUNTIME_CAP_NONE;
    if (!SnapshotRuntime(&backend, &il2cpp, &capabilities) ||
        (capabilities & URK_RUNTIME_CAP_GRAPHICS_DEVICE_TYPE) == 0) {
        return URK_GRAPHICS_DEVICE_UNKNOWN;
    }

    const DWORD mainThreadId = g_unityMainThreadId.load(std::memory_order_acquire);
    if (mainThreadId == 0 || mainThreadId != GetCurrentThreadId())
        return URK_GRAPHICS_DEVICE_UNKNOWN;

    int32_t value = URK_GRAPHICS_DEVICE_UNKNOWN;
    switch (backend) {
        case RuntimeEventsBackend::Il2Cpp: {
            if (!il2cpp)
                return URK_GRAPHICS_DEVICE_UNKNOWN;
            Il2CppRuntimeThreadScope scope(*il2cpp);
            return scope.IsAttached() && ReadIl2CppGraphicsDeviceType(&value) ? value : URK_GRAPHICS_DEVICE_UNKNOWN;
        }
        default:
            return URK_GRAPHICS_DEVICE_UNKNOWN;
    }
}

int RuntimeEvents_IsMainThread() {
    const DWORD mainThreadId = g_unityMainThreadId.load(std::memory_order_acquire);
    return mainThreadId != 0 && mainThreadId == GetCurrentThreadId() ? 1 : 0;
}


