std::string ModLifecycleHeader() {
    return R"URK(#pragma once

#include "sdk/mod_sdk.h"

namespace ModLifecycle {
bool initialize(const URK_ModContext *ctx);
void on_scene_loaded(const URK_SceneInfo *scene);
void on_scene_changed(const URK_SceneInfo *previousScene, const URK_SceneInfo *currentScene);
//@unity{
void on_object_destroy_requested(const URK_ObjectDestroyRequest *request);
//@unity}
void shutdown();
} // namespace ModLifecycle
)URK";
}

std::string ModLifecycleSource(const ModuleProjectOptions &options) {
    const std::string apiField = BackendApiField(options);
    const std::string apiName = options.backendDisplayName;
    std::string apiMacro = apiField;
    for (char &ch : apiMacro)
        ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    std::ostringstream out;
    out << "#include \"mod_lifecycle.h\"\n\n"
        << "#include \"lifecycle/mod_runtime.h\"\n"
        << "#include \"hooks/mod_hooks.h\"\n"
        << "#include \"support/mod_log.h\"\n\n"
        << "#include \"lifecycle/mod_network.h\"\n\n"
        << "#include \"sdk/mod_async.h\"\n\n"
        << "#include <cstddef>\n"
        << "#include <utility>\n\n"
        << "#include \"sdk/runtime_api.h\"\n"
        << "#include \"" << BackendRuntimeHeader(options) << "\"\n\n"
        << "namespace {\n"
        << "const URK_ModContext* g_ctx = nullptr;\n"
        << "bool g_update_registered = false;\n"
        << "bool g_network_initialized = false;\n"
        << "bool g_runtime_started = false;\n"
        << "bool g_initialized = false;\n\n"
        << "URK::coroutines::FlowState g_coroutines{};\n\n"
        << "constexpr std::size_t field_end(std::size_t offset, std::size_t size) {\n"
        << "  return offset + size;\n"
        << "}\n\n"
        << "bool context_has_field(const URK_ModContext* ctx, std::size_t end) {\n"
        << "  return ctx && ctx->size >= end;\n"
        << "}\n\n"
        << "void runtime_update() {\n"
        << "  URK::coroutines::tick(g_coroutines);\n"
        << "  ModRuntime::update();\n"
        << "}\n\n"
        << "bool validate_required_backend(const URK_ModContext* ctx) {\n"
        << "  if (!ctx) return false;\n"
        << "  if (ctx->version < URK_SDK_VERSION) {\n"
        << "    ModLog::error(\"mod context SDK version is too old: got=%d required>=%d\", ctx->version, URK_SDK_VERSION);\n"
        << "    return false;\n"
        << "  }\n"
        << "  if (ctx->size < sizeof(URK_ModContext)) {\n"
        << "    ModLog::error(\"mod context table is too small: got=%u required>=%zu\", ctx->size, sizeof(URK_ModContext));\n"
        << "    return false;\n"
        << "  }\n"
        << "  if (!context_has_field(ctx, field_end(offsetof(URK_ModContext, runtimeBackend), "
           "sizeof(ctx->runtimeBackend)))) {\n"
        << "    ModLog::error(\"mod context is too small for runtimeBackend; refusing to initialize\");\n"
        << "    return false;\n"
        << "  }\n"
        << "  if (ctx->runtimeBackend != "
        << (options.requiredBackendConstant.empty() ? "URK::runtime_backend_unknown" : options.requiredBackendConstant)
        << ") {\n"
        << "    ModLog::error(\"required backend mismatch: generated for " << EscapeString(options.backendDisplayName)
        << " but loader runtimeBackend=%u; refusing to initialize\", ctx->runtimeBackend);\n"
        << "    return false;\n"
        << "  }\n"
        << "  if (!context_has_field(ctx, field_end(offsetof(URK_ModContext, runtimeCapabilities), "
           "sizeof(ctx->runtimeCapabilities)))) {\n"
        << "    ModLog::error(\"mod context is too small for runtimeCapabilities; refusing to initialize\");\n"
        << "    return false;\n"
        << "  }\n"
        << "  if ((ctx->runtimeCapabilities & "
        << (options.requiredCapabilityConstant.empty() ? "URK::runtime_cap_none" : options.requiredCapabilityConstant)
        << ") == 0) {\n"
        << "    ModLog::error(\"required " << EscapeString(options.backendDisplayName)
        << " API capability is missing; refusing to initialize\");\n"
        << "    return false;\n"
        << "  }\n"
        << "  if (!context_has_field(ctx, field_end(offsetof(URK_ModContext, " << apiField << "), sizeof(ctx->"
        << apiField << ")))) {\n"
        << "    ModLog::error(\"mod context is too small for " << apiName << " API table; refusing to initialize\");\n"
        << "    return false;\n"
        << "  }\n"
        << "  if (!ctx->" << apiField << ") {\n"
        << "    ModLog::error(\"required " << apiName << " API table is missing; refusing to initialize\");\n"
        << "    return false;\n"
        << "  }\n"
        << "  if (ctx->" << apiField << "->version < URK_" << apiMacro << "_API_VERSION || ctx->" << apiField << "->size < sizeof(*ctx->" << apiField << ")) {\n"
        << "    ModLog::error(\"required " << apiName
        << " API table is incompatible: version=%d size=%u\", ctx->" << apiField << "->version, ctx->"
        << apiField << "->size);\n"
        << "    return false;\n"
        << "  }\n"
        << "  return true;\n"
        << "}\n"
        << "} // namespace\n\n"
        << "namespace ModAsync {\n"
        << "URK::coroutines::FlowState& flow() { return g_coroutines; }\n"
        << "void spawn(URK::coroutines::Task task) {\n"
        << "  URK::coroutines::spawn(g_coroutines, std::move(task));\n"
        << "}\n"
        << "void cancel_all() { URK::coroutines::cancel_all(g_coroutines); }\n"
        << "} // namespace ModAsync\n\n"
        << "namespace ModLifecycle {\n"
        << "bool initialize(const URK_ModContext* ctx) {\n"
        << "  if (!ctx) return false;\n"
        << "  if (g_initialized) {\n"
        << "    ModLog::error(\"ModInitEx was invoked more than once; refusing duplicate initialization\");\n"
        << "    return false;\n"
        << "  }\n"
        << "  g_ctx = ctx;\n"
        << "  ModLog::initialize(ctx);\n"
        << "  if (!validate_required_backend(ctx)) { g_ctx = nullptr; ModLog::shutdown(); return false; }\n\n"
        << "  URK::set_context(ctx);\n"
        << "  if (!ModNetwork::init(ctx)) { URK::set_context(nullptr); g_ctx = nullptr; ModLog::shutdown(); return false; }\n"
        << "  g_network_initialized = true;\n"
        << "  if (!ModRuntime::start(ctx)) { ModNetwork::shutdown(); g_network_initialized = false; URK::set_context(nullptr); g_ctx = nullptr; ModLog::shutdown(); return false; }\n"
        << "  g_runtime_started = true;\n"
        << "  if (!ModHooks::install(ctx)) { ModRuntime::stop(); g_runtime_started = false; ModNetwork::shutdown(); g_network_initialized = false; URK::set_context(nullptr); g_ctx = nullptr; ModLog::shutdown(); return false; }\n\n"
        << "  if (context_has_field(ctx, field_end(offsetof(URK_ModContext, MainThreadRegister), "
           "sizeof(ctx->MainThreadRegister))) && ctx->MainThreadRegister) {\n"
        << "    g_update_registered = ctx->MainThreadRegister(&runtime_update) != 0;\n"
        << "    if (!g_update_registered)\n"
        << "      ModLog::warn(\"main-thread update registration failed\");\n"
        << "  } else {\n"
        << "    ModLog::warn(\"main-thread dispatcher unavailable; update() will not be called automatically\");\n"
        << "  }\n\n"
        << "  g_initialized = true;\n"
        << "  ModLog::info(\"mod initialized\");\n"
        << "  return true;\n"
        << "}\n\n"
        << "void on_scene_loaded(const URK_SceneInfo* scene) {\n"
        << "  ModRuntime::on_scene_loaded(scene);\n"
        << "}\n\n"
        << "void on_scene_changed(const URK_SceneInfo* previousScene,\n"
        << "                      const URK_SceneInfo* currentScene) {\n"
        << "  ModRuntime::on_scene_changed(previousScene, currentScene);\n"
        << "}\n\n"
        << "//@unity{\n"
        << "void on_object_destroy_requested(const URK_ObjectDestroyRequest* request) {\n"
        << "  ModRuntime::on_object_destroy_requested(request);\n"
        << "}\n"
        << "//@unity}\n\n"
        << "void shutdown() {\n"
        << "  if (!g_initialized && !g_runtime_started && !g_network_initialized) return;\n"
        << "  if (g_update_registered && g_ctx && "
           "context_has_field(g_ctx, field_end(offsetof(URK_ModContext, MainThreadUnregister), "
           "sizeof(g_ctx->MainThreadUnregister))) && "
           "g_ctx->MainThreadUnregister) {\n"
        << "    g_ctx->MainThreadUnregister(&runtime_update);\n"
        << "  }\n"
        << "  g_update_registered = false;\n\n"
        << "  ModAsync::cancel_all();\n"
        << "  ModHooks::uninstall();\n"
        << "  if (g_runtime_started) ModRuntime::stop();\n"
        << "  if (g_network_initialized) ModNetwork::shutdown();\n"
        << "  g_runtime_started = false;\n"
        << "  g_network_initialized = false;\n"
        << "  g_initialized = false;\n"
        << "  ModLog::info(\"mod shutdown\");\n"
        << "  ModLog::shutdown();\n"
        << "  URK::set_context(nullptr);\n"
        << "  g_ctx = nullptr;\n"
        << "}\n"
        << "} // namespace ModLifecycle\n";
    return out.str();
}

std::string ModEntrySource() {
    return R"URK(#include "mod_lifecycle.h"

#include "config/mod_config.h"

#if defined(_WIN32)
#define URK_EXPORT __declspec(dllexport)
#else
#define URK_EXPORT __attribute__((visibility("default")))
#endif

extern "C" URK_EXPORT const URK_ModInfo *URKGetModInfo() {
    static constexpr URK_ModInfo info{
        .projectName = ModConfig::project_name,
        .displayName = ModConfig::display_name,
        .author = ModConfig::author,
        .version = ModConfig::version,
        .url = ModConfig::url,
        .description = ModConfig::description,
    };
    return &info;
}

extern "C" URK_EXPORT int ModInitEx(const URK_ModContext *ctx) {
    return ModLifecycle::initialize(ctx) ? 1 : 0;
}

extern "C" URK_EXPORT void OnSceneLoaded(const URK_SceneInfo *scene) {
    ModLifecycle::on_scene_loaded(scene);
}

extern "C" URK_EXPORT void OnSceneChanged(const URK_SceneInfo *previousScene, const URK_SceneInfo *currentScene) {
    ModLifecycle::on_scene_changed(previousScene, currentScene);
}

//@unity{
extern "C" URK_EXPORT void OnObjectDestroyRequested(const URK_ObjectDestroyRequest *request) {
    ModLifecycle::on_object_destroy_requested(request);
}
//@unity}

extern "C" URK_EXPORT void ModShutdown() {
    ModLifecycle::shutdown();
}
)URK";
}
